#include <cstdio>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vector>

struct Instruction {
    enum class Type {
        Add,
        Sub,
        Right,
        Left,
        Loop,
        EndLoop,
        Write,
        Read,
    };
    Type type;
};

struct AddInsn : public Instruction {
    AddInsn() { type = Type::Add; }
};

struct SubInsn : public Instruction {
    SubInsn() { type = Type::Sub; }
};

struct RightInsn : public Instruction {
    RightInsn() { type = Type::Right; }
};

struct LeftInsn : public Instruction {
    LeftInsn() { type = Type::Left; }
};

struct LoopInsn : public Instruction {
    LoopInsn() { type = Type::Loop; }
};

struct EndLoopInsn : public Instruction {
    EndLoopInsn() { type = Type::EndLoop; }
};

struct WriteInsn : public Instruction {
    WriteInsn() { type = Type::Write; }
};

struct ReadInsn : public Instruction {
    ReadInsn() { type = Type::Read; }
};

struct VM {
    std::unique_ptr<char[50000]> memory;
    std::size_t pointer{0};
};

struct Block {
    std::vector<std::unique_ptr<Instruction>> instructions;
    template <typename T> void append(T &insn) {
        instructions.push_back(std::make_unique(insn));
    }
};

struct Program {
    std::vector<Block> blocks;
};

namespace JIT {

enum class Register8 {
    AL = 0b000,
    BL = 0b011,
    CL = 0b001,
};

enum class Register32 {
    EAX = 0b000,
    EBX = 0b011,
    ECX = 0b001,
    EDX = 0b010,
    ESI = 0b110,
    EDI = 0b111,
};

enum class Register64 {
    RAX = 0b000,
    RBX = 0b011,
    RCX = 0b001,
    RDX = 0b010,
    RSI = 0b110,
    RDI = 0b111,
};

struct Imm8 {
    static const size_t length = 1;
    unsigned char value{0};
    explicit Imm8(unsigned char val) : value(val) {
        bytes.resize(length);
        bytes[0] = val;
    }

    const std::vector<char> &get_bytes() { return bytes; }

  private:
    std::vector<char> bytes;
};

struct Imm32 {
    static const size_t length = 4;
    unsigned int value{0};
    explicit Imm32(unsigned int val) : value(val) {
        bytes.resize(length);
        for (int i = 0; i < length; i++) {
            bytes[i] = val & 0xff;
            val >>= 8;
        }
    }

    const std::vector<char> &get_bytes() { return bytes; }

  private:
    std::vector<char> bytes;
};

struct Imm64 {
    static const size_t length = 8;
    unsigned int value{0};
    explicit Imm64(unsigned int val) : value(val) {
        bytes.resize(length);
        for (int i = 0; i < length; i++) {
            bytes[i] = val & 0xff;
            val >>= 8;
        }
    }

    const std::vector<char> &get_bytes() { return bytes; }

  private:
    std::vector<char> bytes;
};

struct Emitter {
    void ret() { buffer.emplace_back(0xC3); }
    void mov(Register32 dst, Imm32 src) {
        buffer.push_back(0xB8 | (int)dst);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void mov(Register64 dst, Imm64 src) {
        buffer.push_back(0x48);
        buffer.push_back(0xB8 | (int)dst);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void mov(Register64 dst, Register64 src) {
        buffer.push_back(0x48);
        buffer.push_back(0x89);
        buffer.push_back(0xc0 | ((int)dst) << 3 | (int)src);
    }
    /**
     * mov dst, [src]
     */
    void mov_deref(Register8 dst, Register64 src) {
        buffer.push_back(0x8A);
        buffer.push_back(((int)dst) << 3 | (int)src);
    }
    /**
     * mov [dst], src
     */
    void deref_mov(Register64 dst, Register8 src) {
        buffer.push_back(0x88);
        buffer.push_back(((int)src << 3) | (int)dst);
    }
    void add(Register32 dst, Imm32 src) {
        buffer.push_back(0x05);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void add(Register32 dst, Register32 src) {
        buffer.push_back(0x01);
        buffer.push_back(0xC0 | ((int)src) << 3 | (int)dst);
    }
    void sub(Register32 dst, Imm32 src) {
        buffer.push_back(0x81);
        buffer.push_back(0xE8 | (int)dst);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void al_add(Imm8 src) {
        buffer.push_back(0x04);
        auto arg = src.get_bytes();
        buffer.insert(buffer.end(), arg.begin(), arg.end());
    }
    void cmp(Register32 dst, Imm32 src) {
        buffer.push_back(0x3D);
        auto arg = src.get_bytes();
        buffer.insert(buffer.end(), arg.begin(), arg.end());
    }
    void jmp(Imm32 offset) {
        buffer.push_back(0xE9);
        auto arg = offset.get_bytes();
        buffer.insert(buffer.end(), arg.begin(), arg.end());
    }
    void jz(Imm32 offset) {
        buffer.push_back(0x0F);
        buffer.push_back(0x84);
        auto arg = offset.get_bytes();
        buffer.insert(buffer.end(), arg.begin(), arg.end());
    }

    void syscall() {
        buffer.push_back(0x0F);
        buffer.push_back(0x05);
    }

    std::vector<char> get() { return buffer; }

  private:
    std::vector<char> buffer;
};

struct Compiler {
    Compiler() {}
    void compile_add(AddInsn insn, Emitter &emitter) {
        emitter.mov_deref(Register8::AL, Register64::RCX);
        emitter.al_add(Imm8(1));
        emitter.deref_mov(Register64::RCX, Register8::AL);
    }
    void compile_sub(SubInsn insn, Emitter &emitter) {
        emitter.mov_deref(Register8::AL, Register64::RCX);
        emitter.al_add(Imm8(-1));
        emitter.deref_mov(Register64::RCX, Register8::AL);
    }
    void compile_right(RightInsn insn, Emitter &emitter) {
        emitter.add(Register32::ECX, Imm32(1));
    }
    void compile_left(LeftInsn insn, Emitter &emitter) {
        emitter.sub(Register32::ECX, Imm32(1));
    }
    void compile_write(WriteInsn insn, Emitter &emitter) {
        emitter.mov(Register64::RAX, Imm64(1));
        emitter.mov(Register64::RDI, Imm64(1));
        emitter.mov(Register64::RSI, Register64::RCX);
        emitter.mov(Register64::RDX, Imm64(1));
        emitter.syscall();
    }
};

}; // namespace JIT

void *allocate_function(std::size_t size) {
    void *fn_memory = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return fn_memory;
}

void write_function(void *fn_memory) {
    JIT::Emitter emitter;
    emitter.mov(JIT::Register32::EAX, JIT::Imm32(0x45));
    emitter.mov(JIT::Register64::RSI, JIT::Register64::RDI);
    emitter.deref_mov(JIT::Register64::RSI, JIT::Register8::AL);
    emitter.mov(JIT::Register64::RAX, JIT::Imm64(1));
    emitter.mov(JIT::Register64::RDI, JIT::Imm64(1));
    emitter.mov(JIT::Register64::RDX, JIT::Imm64(1));
    emitter.syscall();
    emitter.mov(JIT::Register64::RAX, JIT::Imm64(0));
    emitter.ret();
    std::vector<char> code = emitter.get();
    for (auto c : code) {
        printf("%02x ", c & 0xff);
    }
    puts("");
    memcpy(fn_memory, code.data(), code.size());
}

typedef unsigned long long (*FnPointer)(char *);

FnPointer build_function() {
    void *fn = allocate_function(256);
    write_function(fn);
    return (FnPointer)fn;
}

int main() {
    char buffer[1000];
    FnPointer fn = build_function();
    int x = fn(buffer);
    printf("%d\n", x);
    return 0;
}
