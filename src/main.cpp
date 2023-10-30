#include <cstdio>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <string>
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
    AddInsn(int val) : value(val) { type = Type::Add; }
    int value{0};
};

struct SubInsn : public Instruction {
    SubInsn(int val) : value(val) { type = Type::Sub; }
    int value{0};
};

struct RightInsn : public Instruction {
    RightInsn(int val) : value(val) { type = Type::Right; }
    int value{0};
};

struct LeftInsn : public Instruction {
    LeftInsn(int val) : value(val) { type = Type::Left; }
    int value{0};
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

struct Program {
    std::vector<std::unique_ptr<Instruction>> instructions;
    template <typename T> void append(T &&insn) {
        instructions.push_back(std::make_unique<T>(insn));
    }
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
        buffer.push_back(0xc0 | ((int)src) << 3 | (int)dst);
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
        buffer.push_back(0x81);
        buffer.push_back(0xC0 | (int)dst);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void add(Register32 dst, Register32 src) {
        buffer.push_back(0x01);
        buffer.push_back(0xC0 | ((int)src) << 3 | (int)dst);
    }
    void add(Register64 dst, Imm32 src) {
        buffer.push_back(0x48);
        buffer.push_back(0x81);
        buffer.push_back(0xC0 | (int)dst);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void sub(Register32 dst, Imm32 src) {
        buffer.push_back(0x81);
        buffer.push_back(0xE8 | (int)dst);
        auto imm = src.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void sub(Register64 dst, Imm32 src) {
        buffer.push_back(0x48);
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
    void al_sub(Imm8 src) {
        buffer.push_back(0x2C);
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
    void compile_setup(Emitter &emitter) {
        emitter.mov(Register64::RCX, Register64::RDI);
    }
    void compile_cleanup(Emitter &emitter) {
        emitter.mov(Register64::RAX, Imm64(0));
        emitter.ret();
    }
    void compile_add(AddInsn insn, Emitter &emitter) {
        emitter.mov_deref(Register8::AL, Register64::RCX);
        emitter.al_add(Imm8(insn.value));
        emitter.deref_mov(Register64::RCX, Register8::AL);
    }
    void compile_sub(SubInsn insn, Emitter &emitter) {
        emitter.mov_deref(Register8::AL, Register64::RCX);
        emitter.al_sub(Imm8(insn.value));
        emitter.deref_mov(Register64::RCX, Register8::AL);
    }
    void compile_right(RightInsn insn, Emitter &emitter) {
        emitter.add(Register64::RCX, Imm32(insn.value));
    }
    void compile_left(LeftInsn insn, Emitter &emitter) {
        emitter.sub(Register64::RCX, Imm32(insn.value));
    }
    void compile_write(WriteInsn insn, Emitter &emitter) {
        emitter.mov(Register64::RAX, Imm64(1));
        emitter.mov(Register64::RDI, Imm64(1));
        emitter.mov(Register64::RSI, Register64::RCX);
        emitter.mov(Register64::RDX, Imm64(1));
        emitter.mov(Register64::RBX, Register64::RCX);
        emitter.syscall();
        emitter.mov(Register64::RCX, Register64::RBX);
    }
    void compile_read(ReadInsn insn, Emitter &emitter) {
        // TODO: Implement this.
    }
};

}; // namespace JIT

struct Compiler {
    Compiler() {}

    Program compile_program(std::string code) {
        Program program;
        for (int i = 0; i < code.length(); i++) {
            if (is_add_or_sub(code, i)) {
                int total = 0;
                while (is_add_or_sub(code, i)) {
                    if (code[i] == '+') {
                        total++;
                    } else {
                        total--;
                    }
                    i++;
                }

                // This is necessary to keep the increment in the for loop valid
                i--;

                if (total == 0) {
                    continue;
                }
                if (total > 0) {
                    program.append(AddInsn(total));
                } else {
                    program.append(SubInsn(-total));
                }
                continue;
            }
            if (is_shift(code, i)) {
                int total = 0;
                while (is_shift(code, i)) {
                    if (code[i] == '<') {
                        total--;
                    } else {
                        total++;
                    }
                    i++;
                }

                // This is necessary to keep the increment in the for loop valid
                i--;

                if (total == 0) {
                    continue;
                }
                if (total > 0) {
                    program.append(RightInsn(total));
                } else {
                    program.append(LeftInsn(-total));
                }
                continue;
            }
            if (code[i] == '.') {
                program.append(WriteInsn());
                continue;
            }
            if (code[i] == ',') {
                program.append(ReadInsn());
                continue;
            }
            if (code[i] == '[') {
                program.append(LoopInsn());
                continue;
            }
            if (code[i] == ']') {
                program.append(EndLoopInsn());
                continue;
            }
        }
        return program;
    }

  private:
    bool is_add_or_sub(std::string &code, int pos) {
        return pos < code.length() && (code[pos] == '-' || code[pos] == '+');
    }
    bool is_shift(std::string &code, int pos) {
        return pos < code.length() && (code[pos] == '<' || code[pos] == '>');
    }
    std::vector<JIT::Emitter> emitters;
};

void *allocate_function(std::size_t size) {
    void *fn_memory = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return fn_memory;
}

void write_function(void *fn_memory) {
    JIT::Compiler compiler;
    JIT::Emitter emitter;

    compiler.compile_setup(emitter);

    compiler.compile_add(AddInsn(0x41), emitter);
    compiler.compile_write(WriteInsn(), emitter);
    compiler.compile_right(RightInsn(1), emitter);
    compiler.compile_add(AddInsn(10), emitter);
    compiler.compile_write(WriteInsn(), emitter);

    compiler.compile_cleanup(emitter);

    std::vector<char> code = emitter.get();
#if defined(DEBUG_INTRUCTIONS)
    for (auto c : code) {
        printf("%02x ", c & 0xff);
    }
    puts("");
#endif
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
    memset(buffer, 0, sizeof(buffer));
    FnPointer fn = build_function();
    int x = fn(buffer);
    printf("%d\n", x);
    return 0;
}
