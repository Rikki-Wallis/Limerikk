use pyo3::prelude::*;

struct Board {
    bbs: [u64; 12],
    mailbox: [i8; 64],
}

impl Board {
    fn from_bbs(bbs: [u64; 12]) -> Self {
        let mut mailbox = [-1i8; 64];
        for (piece, &bb) in bbs.iter().enumerate() {
            let mut bb = bb;
            while bb != 0 {
                let sq = bb.trailing_zeros() as usize;
                mailbox[sq] = piece as i8;
                bb &= bb - 1;
            }
        }
        Self { bbs, mailbox }
    }

    fn piece_at(&self, sq: usize) -> Option<i32> {
        let p = self.mailbox[sq];
        if p == -1 { None } else { Some(p as i32) }
    }

    fn remove(&mut self, sq: usize, piece: i32) {
        assert!(self.mailbox[sq] == piece as i8);
        self.bbs[piece as usize] &= !(1u64 << sq);
        self.mailbox[sq] = -1;
    }

    fn add(&mut self, sq: usize, piece: i32) {
        assert!(self.mailbox[sq] == -1);
        self.bbs[piece as usize] |= 1u64 << sq;
        self.mailbox[sq] = piece as i8;
    }
}

/// A Python module implemented in Rust.
#[pymodule]
mod dataloader {
    use std::{fs::File, io::{BufReader, Cursor, Read}};
    use pyo3::prelude::*;
    use numpy::{PyArray1, PyArrayMethods};
    use byteorder::{LittleEndian, ReadBytesExt};
    use rayon::prelude::*;
    use crate::Board;

    const WHITE_PAWN:   i32 = 0;
    const WHITE_KNIGHT: i32 = 1;
    const WHITE_BISHOP: i32 = 2;
    const WHITE_ROOK:   i32 = 3;
    const WHITE_QUEEN:  i32 = 4;
    const WHITE_KING:   i32 = 5;
    const BLACK_PAWN:   i32 = 6;
    const BLACK_KNIGHT: i32 = 7;
    const BLACK_BISHOP: i32 = 8;
    const BLACK_ROOK:   i32 = 9;
    const BLACK_QUEEN:  i32 = 10;
    const BLACK_KING:   i32 = 11;

    #[pyclass]
    struct Dataloader {
        reader: BufReader<File>,
        buffer: Vec<([u64;12], i16, i8)>,
    }

    fn sq_coords(sq: usize) -> (usize, usize) {
        let r = sq >> 3;
        let f = sq & 0b111;

        (r, f)
    }

    fn coords_sq(r: usize, f: usize) -> usize {
        return r * 8 + f;
    }

    fn read_bitboards<R: ReadBytesExt>(reader: &mut R) -> [u64;12] {
        std::array::from_fn(|_| reader.read_u64::<LittleEndian>().unwrap())
    }

    fn read_raw_game(reader: &mut BufReader<File>) -> Option<Vec<u8>> {
        let mut header = [0u8; 4];
        reader.read_exact(&mut header).ok()?;
        let seq_len = u16::from_le_bytes([header[2], header[3]]) as usize;
        let mut data = vec![0u8; 4 + 12 * 8 * 2 + seq_len * 4];
        data[..4].copy_from_slice(&header);
        reader.read_exact(&mut data[4..]).unwrap();
        Some(data)
    }

    fn parse_game(data: &[u8], result: &mut Vec<([u64;12], i16, i8)>) {
        let mut cur = Cursor::new(data);

        let magic = cur.read_i8().unwrap();
        assert!(magic == 67);

        let outcome = cur.read_i8().unwrap();
        assert!(outcome == -1 || outcome == 0 || outcome == 1);

        let seq_len = cur.read_u16::<LittleEndian>().unwrap() as usize;

        let start = read_bitboards(&mut cur);
        let end = read_bitboards(&mut cur);

        let mut board = Board::from_bbs(start);

        for _ in 0..seq_len {
            let mv = cur.read_u32::<LittleEndian>().unwrap();
            let move_start = (mv & 0b111111) as usize;
            let move_end = ((mv >> 6) & 0b111111) as usize;
            let move_flag = (mv >> 12) & 0b111;

            let move_good = ((mv >> 15) & 1) != 0;

            let score = (mv >> 16) as i16;

            if move_good {
                result.push((board.bbs, score, outcome));
            }

            let start_piece = board.piece_at(move_start).unwrap();
            let end_piece = start_piece + (move_flag as i32);

            let capture_square = if start_piece == WHITE_PAWN || start_piece == BLACK_PAWN {
                let (_, fa) = sq_coords(move_start);
                let (rb, fb) = sq_coords(move_end);

                if fa != fb { // capture
                    if board.piece_at(move_end).is_none() { // must have been en passant
                        if start_piece == WHITE_PAWN {
                            coords_sq(rb-1, fb)
                        }
                        else {
                            coords_sq(rb+1, fb)
                        }
                    }
                    else {
                        move_end
                    }
                }
                else {
                    move_end
                }
            }
            else {
                move_end
            };

            board.remove(move_start, start_piece);

            if let Some(piece) = board.piece_at(capture_square) {
                board.remove(capture_square, piece);
            }

            board.add(move_end, end_piece);

            // handle castling
            if start_piece == WHITE_KING || start_piece == BLACK_KING {
                let (ra, fa) = sq_coords(move_start);
                let (rb, fb) = sq_coords(move_end);

                if fa.abs_diff(fb) > 1 {
                    assert!(ra == rb);

                    let rook_piece = start_piece - 2;

                    match fb {
                        6 => {
                            board.remove(coords_sq(ra, 7), rook_piece);
                            board.add(coords_sq(ra, 5), rook_piece);
                        }

                        2 => {
                            board.remove(coords_sq(ra, 0), rook_piece);
                            board.add(coords_sq(ra, 3), rook_piece);
                        }

                        _ => {
                            assert!(false);
                        }
                    }
                }
            }
        }

        assert!(board.bbs == end);
    }

    impl Dataloader {
        fn load_chunk(&mut self) -> bool {
            let mut raw_games: Vec<Vec<u8>> = Vec::with_capacity(CHUNK_SIZE);
            let mut eof = false;

            for _ in 0..CHUNK_SIZE {
                match read_raw_game(&mut self.reader) {
                    Some(raw) => raw_games.push(raw),
                    None => { eof = true; break; }
                }
            }

            let parsed: Vec<Vec<([u64;12], i16, i8)>> = raw_games
                .par_iter()
                .map(|raw| {
                    let mut positions = vec![];
                    parse_game(raw, &mut positions);
                    positions
                })
                .collect();

            let start = self.buffer.len();
            self.buffer.reserve(parsed.iter().map(|v| v.len()).sum());
            for positions in parsed {
                self.buffer.extend(positions);
            }

            use rand::seq::SliceRandom;
            self.buffer[start..].shuffle(&mut rand::rng());

            eof
        }
    }

    const CHUNK_SIZE: usize = 1000;

    const ENEMY_PIECE: [i32;12] = [
        BLACK_PAWN,
        BLACK_KNIGHT,
        BLACK_BISHOP,
        BLACK_ROOK,
        BLACK_QUEEN,
        BLACK_KING,
        WHITE_PAWN,
        WHITE_KNIGHT,
        WHITE_BISHOP,
        WHITE_ROOK,
        WHITE_QUEEN,
        WHITE_KING,
    ];

    #[pymethods]
    impl Dataloader {
        #[new]
        fn new(path: &str) -> Self {
            Self {
                reader: BufReader::with_capacity(1 << 20, File::open(path).unwrap()),
                buffer: vec![]
            }
        }

        fn get_batch<'py>(&mut self, py: Python<'py>, batch_size: usize, blend: f32) -> Option<(Py<PyArray1<i64>>, Py<PyArray1<i64>>, Py<PyArray1<i64>>, Py<PyArray1<i64>>, Py<PyArray1<f32>>)> {
            while self.buffer.len() < batch_size {
                if self.load_chunk() {
                    break;
                }
            }

            if self.buffer.len() < batch_size {
                return None;
            }

            let n_features: usize = self.buffer.iter().rev().take(batch_size)
                .map(|(bbs, _, _)| bbs.iter().map(|&bb| bb.count_ones() as usize).sum::<usize>())
                .sum();

            let wf_arr = PyArray1::<i64>::zeros(py, n_features, false);
            let wi_arr = PyArray1::<i64>::zeros(py, n_features, false);
            let bf_arr = PyArray1::<i64>::zeros(py, n_features, false);
            let bi_arr = PyArray1::<i64>::zeros(py, n_features, false);
            let t_arr  = PyArray1::<f32>::zeros(py, batch_size, false);

            {
                let mut wf = wf_arr.readwrite();
                let mut wi = wi_arr.readwrite();
                let mut bf = bf_arr.readwrite();
                let mut bi = bi_arr.readwrite();
                let mut t  = t_arr.readwrite();

                let wf_s = wf.as_slice_mut().unwrap();
                let wi_s = wi.as_slice_mut().unwrap();
                let bf_s = bf.as_slice_mut().unwrap();
                let bi_s = bi.as_slice_mut().unwrap();
                let t_s  = t.as_slice_mut().unwrap();

                let mut fi = 0usize;

                for b in 0..batch_size {
                    let (bbs, score, outcome) = self.buffer.pop().unwrap();

                    for (piece, &board) in bbs.iter().enumerate() {
                        let mut board = board;
                        while board != 0 {
                            let sq = board.trailing_zeros();
                            board &= board - 1;
                            wf_s[fi] = (piece * 64) as i64 + sq as i64;
                            bf_s[fi] = (ENEMY_PIECE[piece] * 64) as i64 + (sq ^ 56) as i64;
                            wi_s[fi] = b as _;
                            bi_s[fi] = b as _;
                            fi += 1;
                        }
                    }

                    let score_0_1   = 1.0 / (1.0 + (-(score as f32) / 400.0).exp());
                    let outcome_0_1 = (outcome as f32) * 0.5 + 0.5;
                    t_s[b] = (1.0 - blend) * score_0_1 + blend * outcome_0_1;
                }
            }

            Some((
                wf_arr.unbind(),
                wi_arr.unbind(),
                bf_arr.unbind(),
                bi_arr.unbind(),
                t_arr.unbind(),
            ))
        }
    }

    /// Formats the sum of two numbers as string.
    #[pyfunction]
    fn sum_as_string(a: usize, b: usize) -> PyResult<String> {
        Ok((a + b).to_string())
    }
}
