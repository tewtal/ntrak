// SPC700 processor implementation - adapted from ares/component/processor/spc700/
// Original: Copyright (c) 2004-2025 ares team, Near et al (ISC License)

#include "spc700.h"

#define PC r.pc
// YA helper: reads/writes the 16-bit pair (A, Y)
namespace {
  struct YAHelper {
    n8& a;
    n8& y;
    operator n16() const { return (u16)a | ((u16)y << 8); }
    auto operator=(n16 value) -> YAHelper& { 
      a = value & 0xFF; 
      y = (value >> 8) & 0xFF; 
      return *this; 
    }
  };
  inline YAHelper getYA(n8& a, n8& y) { return {a, y}; }
}
#define YA getYA(r.a, r.y)
#define A r.a
#define X r.x
#define Y r.y
#define S r.s
#define P r.p

#define CF r.p.c
#define ZF r.p.z
#define IF r.p.i
#define HF r.p.h
#define BF r.p.b
#define PF r.p.p
#define VF r.p.v
#define NF r.p.n

#define alu (this->*op)

//=== memory.cpp ===

inline auto SPC700::fetch() -> n8 {
  const n16 address = PC++;
  const auto type = opcodeFetchPending ? BusAccessType::Execute : BusAccessType::Read;
  opcodeFetchPending = false;
  return read(address, type);
}

inline auto SPC700::load(n8 address) -> n8 {
  return read(PF << 8 | address, BusAccessType::Read);
}

inline auto SPC700::store(n8 address, n8 data) -> void {
  return write(PF << 8 | address, data, BusAccessType::Write);
}

inline auto SPC700::pull() -> n8 {
  return read(1 << 8 | ++S, BusAccessType::Read);
}

inline auto SPC700::push(n8 data) -> void {
  return write(1 << 8 | S--, data, BusAccessType::Write);
}

//=== algorithms.cpp ===

auto SPC700::algorithmADC(n8 x, n8 y) -> n8 {
  s32 z = x + y + CF;
  CF = z > 0xff;
  ZF = (n8)z == 0;
  HF = (x ^ y ^ z) & 0x10;
  VF = ~(x ^ y) & (x ^ z) & 0x80;
  NF = z & 0x80;
  return z;
}

auto SPC700::algorithmAND(n8 x, n8 y) -> n8 {
  x &= y;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmASL(n8 x) -> n8 {
  CF = x & 0x80;
  x <<= 1;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmCMP(n8 x, n8 y) -> n8 {
  s32 z = x - y;
  CF = z >= 0;
  ZF = (n8)z == 0;
  NF = z & 0x80;
  return x;
}

auto SPC700::algorithmDEC(n8 x) -> n8 {
  x--;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmEOR(n8 x, n8 y) -> n8 {
  x ^= y;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmINC(n8 x) -> n8 {
  x++;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmLD(n8 x, n8 y) -> n8 {
  ZF = y == 0;
  NF = y & 0x80;
  return y;
}

auto SPC700::algorithmLSR(n8 x) -> n8 {
  CF = x & 0x01;
  x >>= 1;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmOR(n8 x, n8 y) -> n8 {
  x |= y;
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmROL(n8 x) -> n8 {
  bool carry = CF;
  CF = x & 0x80;
  x = (x << 1) | (carry ? 1 : 0);
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmROR(n8 x) -> n8 {
  bool carry = CF;
  CF = x & 0x01;
  x = ((carry ? 1 : 0) << 7) | (x >> 1);
  ZF = x == 0;
  NF = x & 0x80;
  return x;
}

auto SPC700::algorithmSBC(n8 x, n8 y) -> n8 {
  return algorithmADC(x, ~y);
}

auto SPC700::algorithmADW(n16 x, n16 y) -> n16 {
  n16 z;
  CF = 0;
  z  = algorithmADC(x, y);
  z |= algorithmADC(x >> 8, y >> 8) << 8;
  ZF = z == 0;
  return z;
}

auto SPC700::algorithmCPW(n16 x, n16 y) -> n16 {
  s32 z = x - y;
  CF = z >= 0;
  ZF = (n16)z == 0;
  NF = z & 0x8000;
  return x;
}

auto SPC700::algorithmLDW(n16 x, n16 y) -> n16 {
  ZF = y == 0;
  NF = y & 0x8000;
  return y;
}

auto SPC700::algorithmSBW(n16 x, n16 y) -> n16 {
  n16 z;
  CF = 1;
  z  = algorithmSBC(x, y);
  z |= algorithmSBC(x >> 8, y >> 8) << 8;
  ZF = z == 0;
  return z;
}

//=== instructions.cpp ===

auto SPC700::instructionAbsoluteBitModify(n3 mode) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  n3 bit = address >> 13;
  address &= 0x1fff;
  n8 data = read(address);
  switch(mode) {
  case 0:  //or addr:bit
    idle();
    CF = CF || static_cast<bool>(data.bit(bit));
    break;
  case 1:  //or !addr:bit
    idle();
    CF = CF || !static_cast<bool>(data.bit(bit));
    break;
  case 2:  //and addr:bit
    CF = CF && static_cast<bool>(data.bit(bit));
    break;
  case 3:  //and !addr:bit
    CF = CF && !static_cast<bool>(data.bit(bit));
    break;
  case 4:  //eor addr:bit
    idle();
    CF = CF != static_cast<bool>(data.bit(bit));
    break;
  case 5:  //ld addr:bit
    CF = data.bit(bit);
    break;
  case 6:  //st addr:bit
    idle();
    data.bit(bit) = CF;
    write(address, data);
    break;
  case 7:  //not addr:bit
    data.bit(bit) ^= 1;
    write(address, data);
    break;
  }
}

auto SPC700::instructionAbsoluteBitSet(n3 bit, bool value) -> void {
  n8 address = fetch();
  n8 data = load(address);
  data.bit(bit) = value;
  store(address, data);
}

auto SPC700::instructionAbsoluteRead(fpb op, n8& target) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  n8 data = read(address);
  target = alu(target, data);
}

auto SPC700::instructionAbsoluteModify(fps op) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  n8 data = read(address);
  write(address, alu(data));
}

auto SPC700::instructionAbsoluteWrite(n8& data) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  read(address);
  write(address, data);
}

auto SPC700::instructionAbsoluteIndexedRead(fpb op, n8& index) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  idle();
  n8 data = read(address + index);
  A = alu(A, data);
}

auto SPC700::instructionAbsoluteIndexedWrite(n8& index) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  idle();
  read(address + index);
  write(address + index, A);
}

auto SPC700::instructionBranch(bool take) -> void {
  n8 data = fetch();
  if(!take) return;
  idle();
  idle();
  PC += (s8)(u8)data;
}

auto SPC700::instructionBranchBit(n3 bit, bool match) -> void {
  n8 address = fetch();
  n8 data = load(address);
  idle();
  n8 displacement = fetch();
  if(static_cast<bool>(data.bit(bit)) != match) return;
  idle();
  idle();
  PC += (s8)(u8)displacement;
}

auto SPC700::instructionBranchNotDirect() -> void {
  n8 address = fetch();
  n8 data = load(address);
  idle();
  n8 displacement = fetch();
  if(A == data) return;
  idle();
  idle();
  PC += (s8)(u8)displacement;
}

auto SPC700::instructionBranchNotDirectDecrement() -> void {
  n8 address = fetch();
  n8 data = load(address);
  store(address, --data);
  n8 displacement = fetch();
  if(data == 0) return;
  idle();
  idle();
  PC += (s8)(u8)displacement;
}

auto SPC700::instructionBranchNotDirectIndexed(n8& index) -> void {
  n8 address = fetch();
  idle();
  n8 data = load(address + index);
  idle();
  n8 displacement = fetch();
  if(A == data) return;
  idle();
  idle();
  PC += (s8)(u8)displacement;
}

auto SPC700::instructionBranchNotYDecrement() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  n8 displacement = fetch();
  if(--Y == 0) return;
  idle();
  idle();
  PC += (s8)(u8)displacement;
}

auto SPC700::instructionBreak() -> void {
  read(PC, BusAccessType::DummyRead);
  push(PC >> 8);
  push(PC >> 0);
  push(P);
  idle();
  n16 address = read(0xffde + 0);
  address |= read(0xffde + 1) << 8;
  PC = address;
  IF = 0;
  BF = 1;
}

auto SPC700::instructionCallAbsolute() -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  idle();
  push(PC >> 8);
  push(PC >> 0);
  idle();
  idle();
  PC = address;
}

auto SPC700::instructionCallPage() -> void {
  n8 address = fetch();
  idle();
  push(PC >> 8);
  push(PC >> 0);
  idle();
  PC = 0xff00 | address;
}

auto SPC700::instructionCallTable(n4 vector) -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  push(PC >> 8);
  push(PC >> 0);
  idle();
  n16 address = 0xffde - (vector << 1);
  n16 pc = read(address + 0);
  pc |= read(address + 1) << 8;
  PC = pc;
}

auto SPC700::instructionComplementCarry() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  CF = !CF;
}

auto SPC700::instructionDecimalAdjustAdd() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  if(CF || A > 0x99) {
    A += 0x60;
    CF = 1;
  }
  if(HF || (A & 15) > 0x09) {
    A += 0x06;
  }
  ZF = A == 0;
  NF = A & 0x80;
}

auto SPC700::instructionDecimalAdjustSub() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  if(!CF || A > 0x99) {
    A -= 0x60;
    CF = 0;
  }
  if(!HF || (A & 15) > 0x09) {
    A -= 0x06;
  }
  ZF = A == 0;
  NF = A & 0x80;
}

auto SPC700::instructionDirectRead(fpb op, n8& target) -> void {
  n8 address = fetch();
  n8 data = load(address);
  target = alu(target, data);
}

auto SPC700::instructionDirectModify(fps op) -> void {
  n8 address = fetch();
  n8 data = load(address);
  store(address, alu(data));
}

auto SPC700::instructionDirectWrite(n8& data) -> void {
  n8 address = fetch();
  load(address);
  store(address, data);
}

auto SPC700::instructionDirectDirectCompare(fpb op) -> void {
  n8 source = fetch();
  n8 rhs = load(source);
  n8 target = fetch();
  n8 lhs = load(target);
  lhs = alu(lhs, rhs);
  idle();
}

auto SPC700::instructionDirectDirectModify(fpb op) -> void {
  n8 source = fetch();
  n8 rhs = load(source);
  n8 target = fetch();
  n8 lhs = load(target);
  lhs = alu(lhs, rhs);
  store(target, lhs);
}

auto SPC700::instructionDirectDirectWrite() -> void {
  n8 source = fetch();
  n8 data = load(source);
  n8 target = fetch();
  store(target, data);
}

auto SPC700::instructionDirectImmediateCompare(fpb op) -> void {
  n8 immediate = fetch();
  n8 address = fetch();
  n8 data = load(address);
  data = alu(data, immediate);
  idle();
}

auto SPC700::instructionDirectImmediateModify(fpb op) -> void {
  n8 immediate = fetch();
  n8 address = fetch();
  n8 data = load(address);
  data = alu(data, immediate);
  store(address, data);
}

auto SPC700::instructionDirectImmediateWrite() -> void {
  n8 immediate = fetch();
  n8 address = fetch();
  load(address);
  store(address, immediate);
}

auto SPC700::instructionDirectCompareWord(fpw op) -> void {
  n8 address = fetch();
  n16 data = load(address + 0);
  data |= load(address + 1) << 8;
  YA = alu(YA, data);
}

auto SPC700::instructionDirectReadWord(fpw op) -> void {
  n8 address = fetch();
  n16 data = load(address + 0);
  idle();
  data |= load(address + 1) << 8;
  YA = alu(YA, data);
}

auto SPC700::instructionDirectModifyWord(s32 adjust) -> void {
  n8 address = fetch();
  n16 data = load(address + 0) + adjust;
  store(address + 0, data >> 0);
  data += load(address + 1) << 8;
  store(address + 1, data >> 8);
  ZF = data == 0;
  NF = data & 0x8000;
}

auto SPC700::instructionDirectWriteWord() -> void {
  n8 address = fetch();
  load(address + 0);
  store(address + 0, A);
  store(address + 1, Y);
}

auto SPC700::instructionDirectIndexedRead(fpb op, n8& target, n8& index) -> void {
  n8 address = fetch();
  idle();
  n8 data = load(address + index);
  target = alu(target, data);
}

auto SPC700::instructionDirectIndexedModify(fps op, n8& index) -> void {
  n8 address = fetch();
  idle();
  n8 data = load(address + index);
  store(address + index, alu(data));
}

auto SPC700::instructionDirectIndexedWrite(n8& data, n8& index) -> void {
  n8 address = fetch();
  idle();
  load(address + index);
  store(address + index, data);
}

auto SPC700::instructionDivide() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  n16 ya = YA;
  //overflow set if quotient >= 256
  HF = (Y & 15) >= (X & 15);
  VF = Y >= X;
  if(Y < (X << 1)) {
    //if quotient is <= 511 (will fit into 9-bit result)
    A = ya / X;
    Y = ya % X;
  } else {
    //otherwise, the quotient won't fit into VF + A
    //this emulates the odd behavior of the S-SMP in this case
    A = 255 - (ya - (X << 9)) / (256 - X);
    Y = X   + (ya - (X << 9)) % (256 - X);
  }
  //result is set based on a (quotient) only
  ZF = A == 0;
  NF = A & 0x80;
}

auto SPC700::instructionExchangeNibble() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  idle();
  idle();
  A = A >> 4 | A << 4;
  ZF = A == 0;
  NF = A & 0x80;
}

auto SPC700::instructionFlagSet(bool& flag, bool value) -> void {
  read(PC, BusAccessType::DummyRead);
  if(&flag == &IF) idle();
  flag = value;
}

auto SPC700::instructionImmediateRead(fpb op, n8& target) -> void {
  n8 data = fetch();
  target = alu(target, data);
}

auto SPC700::instructionImpliedModify(fps op, n8& target) -> void {
  read(PC, BusAccessType::DummyRead);
  target = alu(target);
}

auto SPC700::instructionIndexedIndirectRead(fpb op, n8& index) -> void {
  n8 indirect = fetch();
  idle();
  n16 address = load(indirect + index + 0);
  address |= load(indirect + index + 1) << 8;
  n8 data = read(address);
  A = alu(A, data);
}

auto SPC700::instructionIndexedIndirectWrite(n8& data, n8& index) -> void {
  n8 indirect = fetch();
  idle();
  n16 address = load(indirect + index + 0);
  address |= load(indirect + index + 1) << 8;
  read(address);
  write(address, data);
}

auto SPC700::instructionIndirectIndexedRead(fpb op, n8& index) -> void {
  n8 indirect = fetch();
  idle();
  n16 address = load(indirect + 0);
  address |= load(indirect + 1) << 8;
  n8 data = read(address + index);
  A = alu(A, data);
}

auto SPC700::instructionIndirectIndexedWrite(n8& data, n8& index) -> void {
  n8 indirect = fetch();
  n16 address = load(indirect + 0);
  address |= load(indirect + 1) << 8;
  idle();
  read(address + index);
  write(address + index, data);
}

auto SPC700::instructionIndirectXRead(fpb op) -> void {
  read(PC, BusAccessType::DummyRead);
  n8 data = load(X);
  A = alu(A, data);
}

auto SPC700::instructionIndirectXWrite(n8& data) -> void {
  read(PC, BusAccessType::DummyRead);
  load(X);
  store(X, data);
}

auto SPC700::instructionIndirectXIncrementRead(n8& data) -> void {
  read(PC, BusAccessType::DummyRead);
  data = load(X++);
  idle();  //quirk: consumes extra idle cycle compared to most read instructions
  ZF = data == 0;
  NF = data & 0x80;
}

auto SPC700::instructionIndirectXIncrementWrite(n8& data) -> void {
  read(PC, BusAccessType::DummyRead);
  idle();  //quirk: not a read cycle as with most write instructions
  store(X++, data);
}

auto SPC700::instructionIndirectXCompareIndirectY(fpb op) -> void {
  read(PC, BusAccessType::DummyRead);
  n8 rhs = load(Y);
  n8 lhs = load(X);
  lhs = alu(lhs, rhs);
  idle();
}

auto SPC700::instructionIndirectXWriteIndirectY(fpb op) -> void {
  read(PC, BusAccessType::DummyRead);
  n8 rhs = load(Y);
  n8 lhs = load(X);
  lhs = alu(lhs, rhs);
  store(X, lhs);
}

auto SPC700::instructionJumpAbsolute() -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  PC = address;
}

auto SPC700::instructionJumpIndirectX() -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  idle();
  n16 pc = read(address + X + 0);
  pc |= read(address + X + 1) << 8;
  PC = pc;
}

auto SPC700::instructionMultiply() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  idle();
  n16 ya = Y * A;
  A = ya >> 0;
  Y = ya >> 8;
  //result is set based on y (high-byte) only
  ZF = Y == 0;
  NF = Y & 0x80;
}

auto SPC700::instructionNoOperation() -> void {
  read(PC, BusAccessType::DummyRead);
}

auto SPC700::instructionOverflowClear() -> void {
  read(PC, BusAccessType::DummyRead);
  HF = 0;
  VF = 0;
}

auto SPC700::instructionPull(n8& data) -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  data = pull();
}

auto SPC700::instructionPullP() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  P = pull();
}

auto SPC700::instructionPush(n8 data) -> void {
  read(PC, BusAccessType::DummyRead);
  push(data);
  idle();
}

auto SPC700::instructionReturnInterrupt() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  P = pull();
  n16 address = pull();
  address |= pull() << 8;
  PC = address;
}

auto SPC700::instructionReturnSubroutine() -> void {
  read(PC, BusAccessType::DummyRead);
  idle();
  n16 address = pull();
  address |= pull() << 8;
  PC = address;
}

auto SPC700::instructionStop() -> void {
  r.stop = true;
  while(r.stop && !synchronizing()) {
    read(PC, BusAccessType::DummyRead);
    idle();
  }
}

auto SPC700::instructionTestSetBitsAbsolute(bool set) -> void {
  n16 address = fetch();
  address |= fetch() << 8;
  n8 data = read(address);
  ZF = (A - data) == 0;
  NF = (A - data) & 0x80;
  read(address);
  write(address, set ? data | A : data & ~A);
}

auto SPC700::instructionTransfer(n8& from, n8& to) -> void {
  read(PC, BusAccessType::DummyRead);
  to = from;
  if(&to == &S) return;
  ZF = to == 0;
  NF = to & 0x80;
}

auto SPC700::instructionWait() -> void {
  r.wait = true;
  while(r.wait && !synchronizing()) {
    read(PC, BusAccessType::DummyRead);
    idle();
  }
}

//=== instruction.cpp (dispatch table) ===

#define op(id, name, ...) case id: return instruction##name(__VA_ARGS__);
#define fp(name) &SPC700::algorithm##name

auto SPC700::instruction() -> void {
  opcodeFetchPending = true;
  switch(fetch()) {
  op(0x00, NoOperation)
  op(0x01, CallTable, 0)
  op(0x02, AbsoluteBitSet, 0, true)
  op(0x03, BranchBit, 0, true)
  op(0x04, DirectRead, fp(OR), A)
  op(0x05, AbsoluteRead, fp(OR), A)
  op(0x06, IndirectXRead, fp(OR))
  op(0x07, IndexedIndirectRead, fp(OR), X)
  op(0x08, ImmediateRead, fp(OR), A)
  op(0x09, DirectDirectModify, fp(OR))
  op(0x0a, AbsoluteBitModify, 0)
  op(0x0b, DirectModify, fp(ASL))
  op(0x0c, AbsoluteModify, fp(ASL))
  op(0x0d, Push, P)
  op(0x0e, TestSetBitsAbsolute, true)
  op(0x0f, Break)
  op(0x10, Branch, NF == 0)
  op(0x11, CallTable, 1)
  op(0x12, AbsoluteBitSet, 0, false)
  op(0x13, BranchBit, 0, false)
  op(0x14, DirectIndexedRead, fp(OR), A, X)
  op(0x15, AbsoluteIndexedRead, fp(OR), X)
  op(0x16, AbsoluteIndexedRead, fp(OR), Y)
  op(0x17, IndirectIndexedRead, fp(OR), Y)
  op(0x18, DirectImmediateModify, fp(OR))
  op(0x19, IndirectXWriteIndirectY, fp(OR))
  op(0x1a, DirectModifyWord, -1)
  op(0x1b, DirectIndexedModify, fp(ASL), X)
  op(0x1c, ImpliedModify, fp(ASL), A)
  op(0x1d, ImpliedModify, fp(DEC), X)
  op(0x1e, AbsoluteRead, fp(CMP), X)
  op(0x1f, JumpIndirectX)
  op(0x20, FlagSet, PF, false)
  op(0x21, CallTable, 2)
  op(0x22, AbsoluteBitSet, 1, true)
  op(0x23, BranchBit, 1, true)
  op(0x24, DirectRead, fp(AND), A)
  op(0x25, AbsoluteRead, fp(AND), A)
  op(0x26, IndirectXRead, fp(AND))
  op(0x27, IndexedIndirectRead, fp(AND), X)
  op(0x28, ImmediateRead, fp(AND), A)
  op(0x29, DirectDirectModify, fp(AND))
  op(0x2a, AbsoluteBitModify, 1)
  op(0x2b, DirectModify, fp(ROL))
  op(0x2c, AbsoluteModify, fp(ROL))
  op(0x2d, Push, A)
  op(0x2e, BranchNotDirect)
  op(0x2f, Branch, true)
  op(0x30, Branch, NF == 1)
  op(0x31, CallTable, 3)
  op(0x32, AbsoluteBitSet, 1, false)
  op(0x33, BranchBit, 1, false)
  op(0x34, DirectIndexedRead, fp(AND), A, X)
  op(0x35, AbsoluteIndexedRead, fp(AND), X)
  op(0x36, AbsoluteIndexedRead, fp(AND), Y)
  op(0x37, IndirectIndexedRead, fp(AND), Y)
  op(0x38, DirectImmediateModify, fp(AND))
  op(0x39, IndirectXWriteIndirectY, fp(AND))
  op(0x3a, DirectModifyWord, +1)
  op(0x3b, DirectIndexedModify, fp(ROL), X)
  op(0x3c, ImpliedModify, fp(ROL), A)
  op(0x3d, ImpliedModify, fp(INC), X)
  op(0x3e, DirectRead, fp(CMP), X)
  op(0x3f, CallAbsolute)
  op(0x40, FlagSet, PF, true)
  op(0x41, CallTable, 4)
  op(0x42, AbsoluteBitSet, 2, true)
  op(0x43, BranchBit, 2, true)
  op(0x44, DirectRead, fp(EOR), A)
  op(0x45, AbsoluteRead, fp(EOR), A)
  op(0x46, IndirectXRead, fp(EOR))
  op(0x47, IndexedIndirectRead, fp(EOR), X)
  op(0x48, ImmediateRead, fp(EOR), A)
  op(0x49, DirectDirectModify, fp(EOR))
  op(0x4a, AbsoluteBitModify, 2)
  op(0x4b, DirectModify, fp(LSR))
  op(0x4c, AbsoluteModify, fp(LSR))
  op(0x4d, Push, X)
  op(0x4e, TestSetBitsAbsolute, false)
  op(0x4f, CallPage)
  op(0x50, Branch, VF == 0)
  op(0x51, CallTable, 5)
  op(0x52, AbsoluteBitSet, 2, false)
  op(0x53, BranchBit, 2, false)
  op(0x54, DirectIndexedRead, fp(EOR), A, X)
  op(0x55, AbsoluteIndexedRead, fp(EOR), X)
  op(0x56, AbsoluteIndexedRead, fp(EOR), Y)
  op(0x57, IndirectIndexedRead, fp(EOR), Y)
  op(0x58, DirectImmediateModify, fp(EOR))
  op(0x59, IndirectXWriteIndirectY, fp(EOR))
  op(0x5a, DirectCompareWord, fp(CPW))
  op(0x5b, DirectIndexedModify, fp(LSR), X)
  op(0x5c, ImpliedModify, fp(LSR), A)
  op(0x5d, Transfer, A, X)
  op(0x5e, AbsoluteRead, fp(CMP), Y)
  op(0x5f, JumpAbsolute)
  op(0x60, FlagSet, CF, false)
  op(0x61, CallTable, 6)
  op(0x62, AbsoluteBitSet, 3, true)
  op(0x63, BranchBit, 3, true)
  op(0x64, DirectRead, fp(CMP), A)
  op(0x65, AbsoluteRead, fp(CMP), A)
  op(0x66, IndirectXRead, fp(CMP))
  op(0x67, IndexedIndirectRead, fp(CMP), X)
  op(0x68, ImmediateRead, fp(CMP), A)
  op(0x69, DirectDirectCompare, fp(CMP))
  op(0x6a, AbsoluteBitModify, 3)
  op(0x6b, DirectModify, fp(ROR))
  op(0x6c, AbsoluteModify, fp(ROR))
  op(0x6d, Push, Y)
  op(0x6e, BranchNotDirectDecrement)
  op(0x6f, ReturnSubroutine)
  op(0x70, Branch, VF == 1)
  op(0x71, CallTable, 7)
  op(0x72, AbsoluteBitSet, 3, false)
  op(0x73, BranchBit, 3, false)
  op(0x74, DirectIndexedRead, fp(CMP), A, X)
  op(0x75, AbsoluteIndexedRead, fp(CMP), X)
  op(0x76, AbsoluteIndexedRead, fp(CMP), Y)
  op(0x77, IndirectIndexedRead, fp(CMP), Y)
  op(0x78, DirectImmediateCompare, fp(CMP))
  op(0x79, IndirectXCompareIndirectY, fp(CMP))
  op(0x7a, DirectReadWord, fp(ADW))
  op(0x7b, DirectIndexedModify, fp(ROR), X)
  op(0x7c, ImpliedModify, fp(ROR), A)
  op(0x7d, Transfer, X, A)
  op(0x7e, DirectRead, fp(CMP), Y)
  op(0x7f, ReturnInterrupt)
  op(0x80, FlagSet, CF, true)
  op(0x81, CallTable, 8)
  op(0x82, AbsoluteBitSet, 4, true)
  op(0x83, BranchBit, 4, true)
  op(0x84, DirectRead, fp(ADC), A)
  op(0x85, AbsoluteRead, fp(ADC), A)
  op(0x86, IndirectXRead, fp(ADC))
  op(0x87, IndexedIndirectRead, fp(ADC), X)
  op(0x88, ImmediateRead, fp(ADC), A)
  op(0x89, DirectDirectModify, fp(ADC))
  op(0x8a, AbsoluteBitModify, 4)
  op(0x8b, DirectModify, fp(DEC))
  op(0x8c, AbsoluteModify, fp(DEC))
  op(0x8d, ImmediateRead, fp(LD), Y)
  op(0x8e, PullP)
  op(0x8f, DirectImmediateWrite)
  op(0x90, Branch, CF == 0)
  op(0x91, CallTable, 9)
  op(0x92, AbsoluteBitSet, 4, false)
  op(0x93, BranchBit, 4, false)
  op(0x94, DirectIndexedRead, fp(ADC), A, X)
  op(0x95, AbsoluteIndexedRead, fp(ADC), X)
  op(0x96, AbsoluteIndexedRead, fp(ADC), Y)
  op(0x97, IndirectIndexedRead, fp(ADC), Y)
  op(0x98, DirectImmediateModify, fp(ADC))
  op(0x99, IndirectXWriteIndirectY, fp(ADC))
  op(0x9a, DirectReadWord, fp(SBW))
  op(0x9b, DirectIndexedModify, fp(DEC), X)
  op(0x9c, ImpliedModify, fp(DEC), A)
  op(0x9d, Transfer, S, X)
  op(0x9e, Divide)
  op(0x9f, ExchangeNibble)
  op(0xa0, FlagSet, IF, true)
  op(0xa1, CallTable, 10)
  op(0xa2, AbsoluteBitSet, 5, true)
  op(0xa3, BranchBit, 5, true)
  op(0xa4, DirectRead, fp(SBC), A)
  op(0xa5, AbsoluteRead, fp(SBC), A)
  op(0xa6, IndirectXRead, fp(SBC))
  op(0xa7, IndexedIndirectRead, fp(SBC), X)
  op(0xa8, ImmediateRead, fp(SBC), A)
  op(0xa9, DirectDirectModify, fp(SBC))
  op(0xaa, AbsoluteBitModify, 5)
  op(0xab, DirectModify, fp(INC))
  op(0xac, AbsoluteModify, fp(INC))
  op(0xad, ImmediateRead, fp(CMP), Y)
  op(0xae, Pull, A)
  op(0xaf, IndirectXIncrementWrite, A)
  op(0xb0, Branch, CF == 1)
  op(0xb1, CallTable, 11)
  op(0xb2, AbsoluteBitSet, 5, false)
  op(0xb3, BranchBit, 5, false)
  op(0xb4, DirectIndexedRead, fp(SBC), A, X)
  op(0xb5, AbsoluteIndexedRead, fp(SBC), X)
  op(0xb6, AbsoluteIndexedRead, fp(SBC), Y)
  op(0xb7, IndirectIndexedRead, fp(SBC), Y)
  op(0xb8, DirectImmediateModify, fp(SBC))
  op(0xb9, IndirectXWriteIndirectY, fp(SBC))
  op(0xba, DirectReadWord, fp(LDW))
  op(0xbb, DirectIndexedModify, fp(INC), X)
  op(0xbc, ImpliedModify, fp(INC), A)
  op(0xbd, Transfer, X, S)
  op(0xbe, DecimalAdjustSub)
  op(0xbf, IndirectXIncrementRead, A)
  op(0xc0, FlagSet, IF, false)
  op(0xc1, CallTable, 12)
  op(0xc2, AbsoluteBitSet, 6, true)
  op(0xc3, BranchBit, 6, true)
  op(0xc4, DirectWrite, A)
  op(0xc5, AbsoluteWrite, A)
  op(0xc6, IndirectXWrite, A)
  op(0xc7, IndexedIndirectWrite, A, X)
  op(0xc8, ImmediateRead, fp(CMP), X)
  op(0xc9, AbsoluteWrite, X)
  op(0xca, AbsoluteBitModify, 6)
  op(0xcb, DirectWrite, Y)
  op(0xcc, AbsoluteWrite, Y)
  op(0xcd, ImmediateRead, fp(LD), X)
  op(0xce, Pull, X)
  op(0xcf, Multiply)
  op(0xd0, Branch, ZF == 0)
  op(0xd1, CallTable, 13)
  op(0xd2, AbsoluteBitSet, 6, false)
  op(0xd3, BranchBit, 6, false)
  op(0xd4, DirectIndexedWrite, A, X)
  op(0xd5, AbsoluteIndexedWrite, X)
  op(0xd6, AbsoluteIndexedWrite, Y)
  op(0xd7, IndirectIndexedWrite, A, Y)
  op(0xd8, DirectWrite, X)
  op(0xd9, DirectIndexedWrite, X, Y)
  op(0xda, DirectWriteWord)
  op(0xdb, DirectIndexedWrite, Y, X)
  op(0xdc, ImpliedModify, fp(DEC), Y)
  op(0xdd, Transfer, Y, A)
  op(0xde, BranchNotDirectIndexed, X)
  op(0xdf, DecimalAdjustAdd)
  op(0xe0, OverflowClear)
  op(0xe1, CallTable, 14)
  op(0xe2, AbsoluteBitSet, 7, true)
  op(0xe3, BranchBit, 7, true)
  op(0xe4, DirectRead, fp(LD), A)
  op(0xe5, AbsoluteRead, fp(LD), A)
  op(0xe6, IndirectXRead, fp(LD))
  op(0xe7, IndexedIndirectRead, fp(LD), X)
  op(0xe8, ImmediateRead, fp(LD), A)
  op(0xe9, AbsoluteRead, fp(LD), X)
  op(0xea, AbsoluteBitModify, 7)
  op(0xeb, DirectRead, fp(LD), Y)
  op(0xec, AbsoluteRead, fp(LD), Y)
  op(0xed, ComplementCarry)
  op(0xee, Pull, Y)
  op(0xef, Wait)
  op(0xf0, Branch, ZF == 1)
  op(0xf1, CallTable, 15)
  op(0xf2, AbsoluteBitSet, 7, false)
  op(0xf3, BranchBit, 7, false)
  op(0xf4, DirectIndexedRead, fp(LD), A, X)
  op(0xf5, AbsoluteIndexedRead, fp(LD), X)
  op(0xf6, AbsoluteIndexedRead, fp(LD), Y)
  op(0xf7, IndirectIndexedRead, fp(LD), Y)
  op(0xf8, DirectRead, fp(LD), X)
  op(0xf9, DirectIndexedRead, fp(LD), X, Y)
  op(0xfa, DirectDirectWrite)
  op(0xfb, DirectIndexedRead, fp(LD), Y, X)
  op(0xfc, ImpliedModify, fp(INC), Y)
  op(0xfd, Transfer, A, Y)
  op(0xfe, BranchNotYDecrement)
  op(0xff, Stop)
  }
}

#undef op
#undef fp

//=== power ===

auto SPC700::power() -> void {
  PC = 0x0000;
  YA = 0x0000;
  X = 0x00;
  S = 0xef;
  P = 0x02;

  r.wait = false;
  r.stop = false;
}

#undef PC
#undef YA
#undef A
#undef X
#undef Y
#undef S
#undef P

#undef CF
#undef ZF
#undef IF
#undef HF
#undef BF
#undef PF
#undef VF
#undef NF

#undef alu
