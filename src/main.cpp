#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stack>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <vector>

typedef unsigned long long (*FnPointer)(char *);

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
    virtual void print() = 0;
};

struct AddInsn : public Instruction {
    AddInsn(int val) : value(val) { type = Type::Add; }
    int value{0};
    void print() override { std::cerr << "Add(" << value << ")\n"; }
};

struct SubInsn : public Instruction {
    SubInsn(int val) : value(val) { type = Type::Sub; }
    int value{0};
    void print() override { std::cerr << "Sub(" << value << ")\n"; }
};

struct RightInsn : public Instruction {
    RightInsn(int val) : value(val) { type = Type::Right; }
    int value{0};
    void print() override { std::cerr << "Right(" << value << ")\n"; }
};

struct LeftInsn : public Instruction {
    LeftInsn(int val) : value(val) { type = Type::Left; }
    int value{0};
    void print() override { std::cerr << "Left(" << value << ")\n"; }
};

struct LoopInsn : public Instruction {
    LoopInsn() { type = Type::Loop; }
    void print() override { std::cerr << "Loop\n"; }
};

struct EndLoopInsn : public Instruction {
    EndLoopInsn() { type = Type::EndLoop; }
    void print() override { std::cerr << "EndLoop\n"; }
};

struct WriteInsn : public Instruction {
    WriteInsn() { type = Type::Write; }
    void print() override { std::cerr << "Write\n"; }
};

struct ReadInsn : public Instruction {
    ReadInsn() { type = Type::Read; }
    void print() override { std::cerr << "Read\n"; }
};

struct Block {
    std::vector<std::unique_ptr<Instruction>> instructions;
    template <typename T> void append(T &&insn) {
        instructions.push_back(std::make_unique<T>(insn));
    }
    void print() {
        for (auto &insn : instructions) {
            insn->print();
        }
    }
};

struct Program {
    std::vector<std::unique_ptr<Block>> blocks;
    void append_new_block() { blocks.push_back(std::make_unique<Block>()); }
    template <typename T> void append_insn(T &&insn) {
        blocks.back()->append(std::move(insn));
    }
    void print() {
        int idx = 0;
        for (auto &block : blocks) {
            std::cerr << "Block " << idx << "\n";
            block->print();
            idx++;
        }
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
        // FIXME: This only compares with EAX
        buffer.push_back(0x3D);
        auto arg = src.get_bytes();
        buffer.insert(buffer.end(), arg.begin(), arg.end());
    }
    void cmp_al(Imm8 src) {
        buffer.push_back(0x3C);
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
    void jnz(Imm32 offset) {
        buffer.push_back(0x0F);
        buffer.push_back(0x85);
        auto arg = offset.get_bytes();
        buffer.insert(buffer.end(), arg.begin(), arg.end());
    }

    void syscall() {
        buffer.push_back(0x0F);
        buffer.push_back(0x05);
    }

    std::vector<char> get() { return buffer; }
    /**
     * Returns the length of the instructions already emitted
     */
    std::size_t length() { return buffer.size(); }

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
        emitter.mov(Register64::RAX, Imm64(0));
        emitter.mov(Register64::RDI, Imm64(0));
        emitter.mov(Register64::RSI, Register64::RCX);
        emitter.mov(Register64::RDX, Imm64(1));
        emitter.mov(Register64::RBX, Register64::RCX);
        emitter.syscall();
        emitter.mov(Register64::RCX, Register64::RBX);
    }
    void compile_loop(int offset, Emitter &emitter) {
        emitter.mov_deref(Register8::AL, Register64::RCX);
        emitter.cmp_al(Imm8(0));
        emitter.jz(Imm32(offset));
    }
    void compile_end_loop(int offset, Emitter &emitter) {
        emitter.mov_deref(Register8::AL, Register64::RCX);
        emitter.cmp_al(Imm8(0));
        offset += emitter.length() + 2;
        emitter.jnz(Imm32(-offset));
    }
};

}; // namespace JIT

struct Compiler {
    Compiler() {}

    Program compile_program(std::string code) {
        Program program;
        program.append_new_block();
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
                    program.append_insn(AddInsn(total));
                } else {
                    program.append_insn(SubInsn(-total));
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
                    program.append_insn(RightInsn(total));
                } else {
                    program.append_insn(LeftInsn(-total));
                }
                continue;
            }
            if (code[i] == '.') {
                program.append_insn(WriteInsn());
                continue;
            }
            if (code[i] == ',') {
                program.append_insn(ReadInsn());
                continue;
            }
            if (code[i] == '[') {
                program.append_new_block();
                program.append_insn(LoopInsn());
                program.append_new_block();
                continue;
            }
            if (code[i] == ']') {
                program.append_new_block();
                program.append_insn(EndLoopInsn());
                program.append_new_block();
                continue;
            }
        }
        validate_loops(program);
        return program;
    }

  private:
    void validate_loops(Program &program) {
        int loop_depth = 0;
        for (auto &block : program.blocks) {
            for (auto &insn : block->instructions) {
                if (insn->type == Instruction::Type::Loop) {
                    loop_depth++;
                } else if (insn->type == Instruction::Type::EndLoop) {
                    loop_depth--;
                    if (loop_depth < 0) {
                        std::cerr << "Invalid input program: Unmatched ']'\n";
                        exit(1);
                    }
                }
            }
        }
        if (loop_depth > 0) {
            std::cerr << "Invalid input program: Unmatched '['\n";
            exit(1);
        }
    }
    bool is_add_or_sub(std::string &code, int pos) {
        return pos < code.length() && (code[pos] == '-' || code[pos] == '+');
    }
    bool is_shift(std::string &code, int pos) {
        return pos < code.length() && (code[pos] == '<' || code[pos] == '>');
    }
};

struct JitCompiler {

    JitCompiler() {}

    FnPointer compile(Program &program) {
        setup();
        generate_emitters(program);
        emit_jumps(program);
        cleanup();
        std::vector<char> fn_code;
        for (auto &emitter : emitters) {
            std::vector<char> data = emitter.get();
            fn_code.insert(fn_code.end(), data.begin(), data.end());
        }

        void *fn_memory = allocate_function(fn_code.size() + 1);
        memcpy(fn_memory, fn_code.data(), fn_code.size());
        return (FnPointer)fn_memory;
    }

  private:
    void generate_emitters(Program &program) {
        for (auto &block : program.blocks) {
            emitters.push_back(JIT::Emitter());
            for (auto &insn : block->instructions) {
                process_instruction(insn.get());
            }
        }
    }

    void emit_jumps(Program &program) {
        for (int i = 0; i < program.blocks.size(); i++) {
            if (program.blocks[i]->instructions.empty()) {
                continue;
            }
            auto &insn = program.blocks[i]->instructions.front();
            if (insn->type == Instruction::Type::Loop) {
                i = emit_jumps(program, i);
            }
        }
    }
    int emit_jumps(Program &program, int position) {
        int length = 0;
        for (int i = position + 1; i < program.blocks.size(); i++) {
            if (program.blocks[i]->instructions.empty()) {
                continue;
            }
            auto &insn = program.blocks[i]->instructions.front();
            if (insn->type == Instruction::Type::Loop) {
                int destination = emit_jumps(program, i);
                for (int j = i; j <= destination; j++) {
                    length += emitters[j + 1].length();
                }
                i = destination;
            } else if (insn->type == Instruction::Type::EndLoop) {
                insn_compiler.compile_loop(length, emitters[position + 1]);
                length += emitters[position + 1].length();
                insn_compiler.compile_end_loop(length, emitters[i + 1]);
                return i;
            } else {
                length += emitters[i + 1].length();
            }
        }
        std::cerr << "I don't know what to do here\n";
        exit(0);
    }

    void *allocate_function(std::size_t size) {
        void *fn_memory = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return fn_memory;
    }
    void setup() {
        JIT::Emitter emitter;
        insn_compiler.compile_setup(emitter);
        emitters.push_back(emitter);
    }
    void cleanup() {
        JIT::Emitter emitter;
        insn_compiler.compile_cleanup(emitter);
        emitters.push_back(emitter);
    }
    void process_instruction(Instruction *insn) {
        switch (insn->type) {
        case Instruction::Type::Add: {
            insn_compiler.compile_add(*static_cast<AddInsn *>(insn),
                                      emitters.back());
            break;
        }
        case Instruction::Type::Sub: {
            insn_compiler.compile_sub(*static_cast<SubInsn *>(insn),
                                      emitters.back());
            break;
        }
        case Instruction::Type::Right: {
            insn_compiler.compile_right(*static_cast<RightInsn *>(insn),
                                        emitters.back());
            break;
        }
        case Instruction::Type::Left: {
            insn_compiler.compile_left(*static_cast<LeftInsn *>(insn),
                                       emitters.back());
            break;
        }
        case Instruction::Type::Read: {
            insn_compiler.compile_read(*static_cast<ReadInsn *>(insn),
                                       emitters.back());
            break;
        }
        case Instruction::Type::Write: {
            insn_compiler.compile_write(*static_cast<WriteInsn *>(insn),
                                        emitters.back());
            break;
        }
        case Instruction::Type::Loop: {
            break;
        }
        case Instruction::Type::EndLoop: {
            break;
        }
        }
    }
    std::vector<JIT::Emitter> emitters;
    JIT::Compiler insn_compiler;
};

struct Interpreter {
    int run_program(std::string &&code) {
        this->code = code;
        Program program = compiler.compile_program(code);
        /* program.print(); */
        FnPointer fn = jit_compiler.compile(program);
        vm_buffer = new char[50000];
        memset(vm_buffer, 0, sizeof(char[50000]));
        int result = fn(vm_buffer);
        delete vm_buffer;
        return result;
    }

  private:
    char *vm_buffer;
    std::string code;
    JitCompiler jit_compiler;
    Compiler compiler;
};

std::string read_file(const std::string &filePath) {
    std::ifstream file(filePath); // Open the file stream

    if (!file.is_open()) {
        std::cerr << "Error: Could not open the file: " << filePath
                  << std::endl;
        return ""; // Return an empty string to indicate failure
    }

    std::string content; // String to hold the file content
    std::string line;    // String to read each line from the file

    // Read the file line by line and append it to the content string
    while (std::getline(file, line)) {
        content += line + "\n";
    }

    file.close(); // Close the file stream

    return content;
}

std::string process_input(std::string &input) {
    std::string result;
    for (char c : input) {
        if (c == '+') {
            result += c;
        }
        if (c == '-') {
            result += c;
        }
        if (c == '>') {
            result += c;
        }
        if (c == '<') {
            result += c;
        }
        if (c == '[') {
            result += c;
        }
        if (c == ']') {
            result += c;
        }
        if (c == '.') {
            result += c;
        }
        if (c == ',') {
            result += c;
        }
    }
    return result;
}

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <filename>\n";
        return 1;
    }
    std::string filename = argv[1];
    std::string input = read_file(filename);
    input = process_input(input);
    Interpreter interpreter;
    interpreter.run_program(input.c_str());
    return 0;
}
