/*
 * Copyright (C) 2020 Sam Kumar <samkumar@cs.berkeley.edu>
 * Copyright (C) 2020 University of California, Berkeley
 * All rights reserved.
 *
 * This file is part of MAGE.
 *
 * MAGE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MAGE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MAGE.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * The crypto logic is lifted from gc/halfgate_eva.h, gc/halfgate_gen.h, and
 * garble/garble_gates_halfgates.h in EMP-toolkit, but it has been modified to
 * work with the rest of MAGE.
 */

#ifndef MAGE_SCHEMES_HALFGATES_HPP_
#define MAGE_SCHEMES_HALFGATES_HPP_

#include <cstdint>
#include <string>
#include <vector>
#include "crypto/block.hpp"
#include "crypto/mitccrh.hpp"
#include "crypto/prg.hpp"
#include "util/binaryfile.hpp"

namespace mage::schemes {
    class HalfGatesGarbler {
    public:
        using Wire = crypto::block;

        HalfGatesGarbler(std::string input_file, std::string output_file, int conn_fd)
            : global_id(0), input_reader(input_file.c_str()), output_writer(output_file.c_str()), conn_reader(conn_fd), conn_writer(conn_fd) {
            crypto::PRG tmp;
            tmp.random_block(&this->seed);
            Wire a;
            this->set_delta(a);
            tmp.random_block(&this->start_point);
            this->mitccrh.setS(this->start_point);

            this->conn_writer.write<Wire>() = this->start_point;

            Wire input_seed;
            this->prg.random_block(&input_seed);
            this->conn_writer.write<Wire>() = input_seed;
            this->shared_prg.set_seed(input_seed);
            this->conn_writer.flush();
        }

        // HACK: assume all output goes to the garbler
        ~HalfGatesGarbler() {
            for (std::uint64_t i = 0; i != this->output_label_lsbs.size(); i++) {
                bool evaluator_lsb = this->conn_reader.read<bool>();
                bool result = (this->output_label_lsbs[i] != evaluator_lsb);
                std::uint8_t output_bit = result ? 0x1 : 0x0;
                this->output_writer.write1(output_bit);
            }
        }

        // HACK: assume all input comes from the garbler
        void input(Wire* data, unsigned int length) {
            this->shared_prg.random_block(data, length);
            for (unsigned int i = 0; i != length; i++) {
                std::uint8_t bit = this->input_reader.read1();
                 if (bit != 0) {
                     data[i] = crypto::xorBlocks(data[i], this->delta);
                 }
            }
        }

        // HACK: assume all output goes to the garbler
        void output(const Wire* data, unsigned int length) {
            for (unsigned int i = 0; i != length; i++) {
                bool lsb = crypto::getLSB(data[i]);
                this->output_label_lsbs.push_back(lsb);
            }
        }

        void op_and(Wire& output, const Wire& input1, const Wire& input2) {
            crypto::block out[2];
            crypto::block table[2];
            table[0] = this->conn_reader.read<crypto::block>();
            table[1] = this->conn_reader.read<crypto::block>();
            if (mitccrh.key_used == KS_BATCH_N) {
                mitccrh.renew_ks(this->global_id);
            }
            garble_gate_garble_halfgates(input1, crypto::xorBlocks(input1, this->delta), input2, crypto::xorBlocks(input2, this->delta), &out[0], &out[1], this->delta, table, &this->mitccrh);
            this->global_id++;
            this->conn_writer.write<crypto::block>() = table[0];
            this->conn_writer.write<crypto::block>() = table[1];
            output = out[0];
        }

        void op_xor(Wire& output, const Wire& input1, const Wire& input2) {
            output = crypto::xorBlocks(input1, input2);
        }

        void op_not(Wire& output, const Wire& input) {
            output = crypto::xorBlocks(input, this->public_constants[1]);
        }

        void op_xnor(Wire& output, const Wire& input1, const Wire& input2) {
            output = crypto::xorBlocks(crypto::xorBlocks(input1, input2), this->public_constants[1]);
        }

        void op_copy(Wire& output, const Wire& input) {
            output = input;
        }

        void one(Wire& output) const {
            output = this->public_constants[1];
        }

        void zero(Wire& output) const {
            output = this->public_constants[0];
        }

    private:
        void set_delta(const Wire& x) {
            this->delta = crypto::make_delta(x);
            crypto::PRG tmp(crypto::fix_key);
            tmp.random_block(this->public_constants, 2);
            this->public_constants[1] = crypto::xorBlocks(this->public_constants[1], this->delta);
        }

        static inline void garble_gate_garble_halfgates(crypto::block LA0, crypto::block A1, crypto::block LB0, crypto::block B1, crypto::block *out0, crypto::block *out1, crypto::block delta, crypto::block *table, crypto::MiTCCRH *mitccrh) {
            long pa = crypto::getLSB(LA0);
            long pb = crypto::getLSB(LB0);
            crypto::block HLA0, HA1, HLB0, HB1;
            crypto::block tmp, W0;

            crypto::block H[4];
            mitccrh->k2_h4(LA0, A1, LB0, B1, H);
            HLA0 = H[0];
            HA1 = H[1];
            HLB0 = H[2];
            HB1 = H[3];

            table[0] = crypto::xorBlocks(HLA0, HA1);
            if (pb)
                table[0] = crypto::xorBlocks(table[0], delta);
            W0 = HLA0;
            if (pa)
                W0 = crypto::xorBlocks(W0, table[0]);
            tmp = crypto::xorBlocks(HLB0, HB1);
            table[1] = crypto::xorBlocks(tmp, LA0);
            W0 = crypto::xorBlocks(W0, HLB0);
            if (pb)
                W0 = crypto::xorBlocks(W0, tmp);

            *out0 = W0;
            *out1 = crypto::xorBlocks(*out0, delta);
        }

        util::BinaryFileReader input_reader;
        util::BinaryFileWriter output_writer;
        util::BufferedFileReader<false> conn_reader;
        util::BufferedFileWriter<false> conn_writer;
        std::vector<bool> output_label_lsbs;

        std::int64_t global_id;
        Wire delta;
        Wire start_point;
        Wire seed;
        Wire public_constants[2];
        crypto::MiTCCRH mitccrh;

        crypto::PRG prg;
        crypto::PRG shared_prg;
    };

    class HalfGatesEvaluator {
    public:
        using Wire = crypto::block;

        HalfGatesEvaluator(std::string input_file, int conn_fd)
            : global_id(0), input_reader(input_file.c_str()), conn_reader(conn_fd), conn_writer(conn_fd) {
            crypto::PRG tmp(crypto::fix_key);
            tmp.random_block(this->public_constants, 2);
            tmp.random_block(&this->start_point);
            mitccrh.start_point = this->start_point;

            this->seed = this->conn_reader.read<Wire>();
            this->start_point = this->conn_reader.read<Wire>();
            this->mitccrh.setS(this->start_point);

            crypto::block input_seed = this->conn_reader.read<crypto::block>();
            this->shared_prg.set_seed(input_seed);
        }

        // HACK: assume all input comes from the garbler
        void input(Wire* data, unsigned int length) {
            this->shared_prg.random_block(data, length);
        }

        // HACK: assume all output goes to the garbler
        void output(const Wire* data, unsigned int length) {
            for (unsigned int i = 0; i != length; i++) {
                bool lsb = crypto::getLSB(data[i]);
                this->conn_writer.write<bool>() = lsb;
            }
        }

        void op_and(Wire& output, const Wire& input1, const Wire& input2) {
            crypto::block table[2];
            if (mitccrh.key_used == KS_BATCH_N) {
                mitccrh.renew_ks(this->global_id);
            }
            garble_gate_eval_halfgates(input1, input2, &output, table, &this->mitccrh);
        }

        void op_xor(Wire& output, const Wire& input1, const Wire& input2) {
            output = crypto::xorBlocks(input1, input2);
        }

        void op_not(Wire& output, const Wire& input) {
            output = crypto::xorBlocks(input, this->public_constants[1]);
        }

        void op_xnor(Wire& output, const Wire& input1, const Wire& input2) {
            output = crypto::xorBlocks(crypto::xorBlocks(input1, input2), this->public_constants[1]);
        }

        void op_copy(Wire& output, const Wire& input) {
            output = input;
        }

        void one(Wire& output) const {
            output = this->public_constants[1];
        }

        void zero(Wire& output) const {
            output = this->public_constants[0];
        }

    private:
        static inline void garble_gate_eval_halfgates(crypto::block A, crypto::block B, crypto::block *out, const crypto::block *table, crypto::MiTCCRH *mitccrh) {
            crypto::block HA, HB, W;
            int sa, sb;

            sa = crypto::getLSB(A);
            sb = crypto::getLSB(B);

            crypto::block H[2];
            mitccrh->k2_h2(A, B, H);
            HA = H[0];
            HB = H[1];

            W = crypto::xorBlocks(HA, HB);
            if (sa)
                W = crypto::xorBlocks(W, table[0]);
            if (sb) {
                W = crypto::xorBlocks(W, table[1]);
                W = crypto::xorBlocks(W, A);
            }
            *out = W;
        }

        util::BinaryFileReader input_reader;
        util::BufferedFileReader<false> conn_reader;
        util::BufferedFileWriter<false> conn_writer;

        std::int64_t global_id;
        Wire start_point;
        Wire seed;
        Wire public_constants[2];
        crypto::MiTCCRH mitccrh;

        crypto::PRG shared_prg;
    };
}

#endif