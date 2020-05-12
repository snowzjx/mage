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

#include "engine/engine.hpp"
#include <cstdint>
#include <cstdlib>
#include "addr.hpp"
#include "instruction.hpp"
#include "opcode.hpp"
#include "schemes/plaintext.hpp"

namespace mage::engine {
    template <typename Protocol>
    void Engine<Protocol>::execute_public_constant(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        BitWidth width = phys.constant.width;
        std::uint64_t constant = phys.constant.constant;

        for (BitWidth i = 0; i != width; i++) {
            if ((constant & 0x1) == 0) {
                this->protocol.zero(output[i]);
            } else {
                this->protocol.one(output[i]);
            }
            constant = constant >> 1;
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_int_add(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        typename Protocol::Wire temp1;
        typename Protocol::Wire temp2;
        typename Protocol::Wire temp3;

        typename Protocol::Wire carry;
        this->protocol.zero(carry);
        this->protocol.op_copy(temp1, input1[0]);
        this->protocol.op_copy(temp2, input2[0]);
        this->protocol.op_xor(output[0], temp1, temp2);
        for (BitWidth i = 1; i != width; i++) {
            /* Calculate carry from previous adder. */
            this->protocol.op_and(temp3, temp1, temp2);
            this->protocol.op_xor(carry, carry, temp3);

            this->protocol.op_xor(temp1, input1[i], carry);
            this->protocol.op_xor(temp2, input2[i], carry);
            this->protocol.op_xor(output[i], temp1, input2[i]);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_int_increment(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input = &this->memory[phys.one_arg.input1];
        BitWidth width = phys.one_arg.width;

        typename Protocol::Wire carry;
        this->protocol.op_not(output[0], input[0]);
        this->protocol.op_copy(carry, input[0]);
        if (width == 1) {
            return;
        }
        for (BitWidth i = 1; i != width - 1; i++) {
            this->protocol.op_xor(output[i], input[i], carry);
            this->protocol.op_and(carry, carry, input[i]);
        }
        this->protocol.op_xor(output[width - 1], input[width - 1], carry);
        // skip computing the final output carry
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_int_sub(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        typename Protocol::Wire temp1;
        typename Protocol::Wire temp2;
        typename Protocol::Wire temp3;

        typename Protocol::Wire borrow;
        this->protocol.zero(borrow);
        this->protocol.op_copy(temp1, input1[0]);
        this->protocol.op_copy(temp2, input2[0]);
        this->protocol.op_xor(output[0], temp1, temp2);
        for (BitWidth i = 1; i != width; i++) {
            /* Calculate carry from previous adder. */
            this->protocol.op_and(temp3, temp1, temp2);
            this->protocol.op_xor(borrow, borrow, temp3);

            this->protocol.op_xor(temp1, input1[i], input2[i]);
            this->protocol.op_xor(temp2, input2[i], borrow);
            this->protocol.op_xor(output[i], temp1, borrow);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_int_decrement(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input = &this->memory[phys.one_arg.input1];
        BitWidth width = phys.one_arg.width;

        typename Protocol::Wire borrow;
        this->protocol.op_not(borrow, input[0]);
        this->protocol.op_copy(output[0], borrow);
        if (width == 1) {
            return;
        }
        for (BitWidth i = 1; i != width - 1; i++) {
            this->protocol.op_xor(output[i], input[i], borrow);
            this->protocol.op_and(borrow, borrow, output[i]);
        }
        this->protocol.op_xor(output[width - 1], input[width - 1], borrow);
        // skip computing the final output carry
    }

    /* Based on https://github.com/samee/obliv-c/blob/obliv-c/src/ext/oblivc/obliv_bits.c */
    template <typename Protocol>
    void Engine<Protocol>::execute_int_less(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        typename Protocol::Wire result;

        typename Protocol::Wire temp1;
        typename Protocol::Wire temp2;
        typename Protocol::Wire temp3;

        this->protocol.op_xor(temp1, input1[0], input2[0]);
        this->protocol.op_and(result, temp1, input2[0]);
        for (BitWidth i = 1; i != width; i++) {
            this->protocol.op_xor(temp1, input1[i], input2[i]);
            this->protocol.op_xor(temp2, input2[i], result);
            this->protocol.op_and(temp3, temp1, temp2);
            this->protocol.op_xor(result, result, temp3);
        }

        this->protocol.op_copy(*output, result);
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_equal(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        typename Protocol::Wire result;
        this->protocol.op_xnor(result, input1[0], input2[0]);

        typename Protocol::Wire temp;
        for (BitWidth i = 1; i != width; i++) {
            this->protocol.op_xnor(temp, input1[i], input2[i]);
            this->protocol.op_and(result, result, temp);
        }
        this->protocol.op_copy(*output, result);
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_is_zero(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input = &this->memory[phys.one_arg.input1];
        BitWidth width = phys.one_arg.width;

        typename Protocol::Wire result;
        this->protocol.op_copy(result, input[0]);

        typename Protocol::Wire temp;
        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_not(temp, input[i]);
            this->protocol.op_and(result, result, temp);
        }
        this->protocol.op_copy(*output, result);
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_non_zero(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input = &this->memory[phys.one_arg.input1];
        BitWidth width = phys.one_arg.width;

        typename Protocol::Wire result;
        this->protocol.op_copy(result, input[0]);

        typename Protocol::Wire temp;
        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_not(temp, input[i]);
            this->protocol.op_and(result, result, temp);
        }
        this->protocol.op_not(*output, result);
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_bit_not(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input = &this->memory[phys.one_arg.input1];
        BitWidth width = phys.one_arg.width;

        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_not(output[i], input[i]);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_bit_and(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_and(output[i], input1[i], input2[i]);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_bit_or(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        typename Protocol::Wire temp1;
        typename Protocol::Wire temp2;
        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_xor(temp1, input1[i], input2[i]);
            this->protocol.op_and(temp2, input1[i], input2[i]);
            this->protocol.op_xor(output[i], temp1, temp2);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_bit_xor(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.two_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.two_args.input2];
        BitWidth width = phys.two_args.width;

        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_xor(output[i], input1[i], input2[i]);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute_value_select(const PackedPhysInstruction& phys) {
        typename Protocol::Wire* output = &this->memory[phys.header.output];
        typename Protocol::Wire* input1 = &this->memory[phys.three_args.input1];
        typename Protocol::Wire* input2 = &this->memory[phys.three_args.input2];
        typename Protocol::Wire* input3 = &this->memory[phys.three_args.input3];
        BitWidth width = phys.two_args.width;

        typename Protocol::Wire selector;
        this->protocol.op_copy(selector, *input3);

        typename Protocol::Wire different;
        typename Protocol::Wire temp;
        for (BitWidth i = 0; i != width; i++) {
            this->protocol.op_xor(different, input1[i], input2[i]);
            this->protocol.op_and(temp, different, selector);
            this->protocol.op_xor(output[i], temp, input1[i]);
        }
    }

    template <typename Protocol>
    void Engine<Protocol>::execute(const PackedPhysInstruction& phys) {
        switch (phys.header.operation) {
        case OpCode::PublicConstant:
            this->execute_public_constant(phys);
            break;
        case OpCode::IntAdd:
            this->execute_int_add(phys);
            break;
        case OpCode::IntIncrement:
            this->execute_int_increment(phys);
            break;
        case OpCode::IntSub:
            this->execute_int_sub(phys);
            break;
        case OpCode::IntDecrement:
            this->execute_int_decrement(phys);
            break;
        case OpCode::IntLess:
            this->execute_int_less(phys);
            break;
        case OpCode::Equal:
            this->execute_equal(phys);
            break;
        case OpCode::IsZero:
            this->execute_is_zero(phys);
            break;
        case OpCode::NonZero:
            this->execute_non_zero(phys);
            break;
        case OpCode::BitNOT:
            this->execute_bit_not(phys);
            break;
        case OpCode::BitAND:
            this->execute_bit_and(phys);
            break;
        case OpCode::BitOR:
            this->execute_bit_or(phys);
            break;
        case OpCode::BitXOR:
            this->execute_bit_xor(phys);
            break;
        case OpCode::ValueSelect:
            this->execute_value_select(phys);
            break;
        default:
            std::abort();
        }
    }

    /* Explicitly instantiate Engine template for each protocol. */
    template class Engine<schemes::Plaintext>;
}
