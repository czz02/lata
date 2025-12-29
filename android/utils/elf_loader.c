#include "elf_loader.h"
#include "function_wrap.h"
#include "include/elf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct {
    FILE *fp;
    Elf64_Ehdr ehdr;
    Elf64_Shdr *shdrs;
    char *shstrtab;
} Filedata;

static inline uint32_t read_inst(uint8_t *base, uint64_t offset)
{
    return *(uint32_t *)(base + offset);
}

static inline void write_inst(uint8_t *base, uint64_t offset, uint32_t inst)
{
    *(uint32_t *)(base + offset) = inst;
}

static int read_section_headers(Filedata *filedata)
{
    FILE *fp = filedata->fp;
    Elf64_Ehdr *ehdr = &filedata->ehdr;

    // 读取 ELF 文件头
    fread(ehdr, 1, sizeof(Elf64_Ehdr), fp);

    // 验证 ELF 魔数
    if (ehdr->e_ident[EI_MAG0] != 0x7f || ehdr->e_ident[EI_MAG1] != 'E' ||
        ehdr->e_ident[EI_MAG2] != 'L' || ehdr->e_ident[EI_MAG3] != 'F') {
        return -1;
    }

    // 读取所有节区头
    filedata->shdrs = malloc(sizeof(Elf64_Shdr) * ehdr->e_shnum);
    if (!filedata->shdrs) {
        return -1;
    }
    fseek(fp, ehdr->e_shoff, SEEK_SET);
    fread(filedata->shdrs, sizeof(Elf64_Shdr), ehdr->e_shnum, fp);

    // 检查节区头字符串表
    if (ehdr->e_shstrndx == SHN_UNDEF) {
        return -1;
    }

    // 读取节区名字符串表
    Elf64_Shdr *shstrtab_hdr = &filedata->shdrs[ehdr->e_shstrndx];
    filedata->shstrtab = malloc(shstrtab_hdr->sh_size);
    if (!filedata->shstrtab) {
        return -1;
    }
    fseek(fp, shstrtab_hdr->sh_offset, SEEK_SET);
    fread(filedata->shstrtab, 1, shstrtab_hdr->sh_size, fp);

    return 0;
}

static int load_symbols(Filedata *filedata, void *addr)
{
    FILE *fp = filedata->fp;
    Elf64_Shdr *shdrs = filedata->shdrs;
    char *shstrtab = filedata->shstrtab;
    int shnum = filedata->ehdr.e_shnum;

    // 外部声明符号映射函数
    extern void wrap_name_add(const char *name, uint64_t func_addr);

    // 查找 .symtab 和 .strtab
    Elf64_Sym *symtab = NULL;
    size_t sym_count = 0;
    char *strtab = NULL;

    for (int i = 0; i < shnum; i++) {
        const char *name = shstrtab + shdrs[i].sh_name;

        if (strcmp(name, ".symtab") == 0) {
            sym_count = shdrs[i].sh_size / sizeof(Elf64_Sym);
            symtab = malloc(shdrs[i].sh_size);
            if (!symtab) {
                return -1;
            }
            fseek(fp, shdrs[i].sh_offset, SEEK_SET);
            fread(symtab, sizeof(Elf64_Sym), sym_count, fp);
        } else if (strcmp(name, ".strtab") == 0) {
            strtab = malloc(shdrs[i].sh_size);
            if (!strtab) {
                free(symtab);
                return -1;
            }
            fseek(fp, shdrs[i].sh_offset, SEEK_SET);
            fread(strtab, 1, shdrs[i].sh_size, fp);
        }
    }

    if (!symtab || !strtab) {
        free(symtab);
        free(strtab);
        return 0; // 没有符号表不是错误
    }

    // 遍历符号表，建立名称到地址的映射
    for (size_t i = 0; i < sym_count; i++) {
        Elf64_Sym *sym = &symtab[i];

        // 只处理函数符号且在 .text 段中的符号
        if (ELF64_ST_TYPE(sym->st_info) == STT_FUNC &&
            sym->st_shndx != SHN_UNDEF) {
            const char *sym_name = strtab + sym->st_name;
            uint64_t func_addr = (uint64_t)addr + sym->st_value;

            wrap_name_add(sym_name, func_addr);

            const char *alias_name = NULL;
            if (strcmp(sym_name, "__memset_lsx") == 0) {
                alias_name = "memset";
            } else if (strcmp(sym_name, "__memcmp_lsx") == 0) {
                alias_name = "memcmp";
            } else if (strcmp(sym_name, "__memcpy_lsx") == 0) {
                alias_name = "memcpy";
            } else if (strcmp(sym_name, "__memmove_lsx") == 0) {
                alias_name = "memmove";
            }
            if (alias_name != NULL) {
                wrap_name_add(alias_name, func_addr);
            }
        }
    }

    free(symtab);
    free(strtab);
    return 0;
}

static uint32_t extractBits(uint64_t v, uint32_t begin, uint32_t end) {
  return begin == 63 ? v >> end : (v & ((1ULL << (begin + 1)) - 1)) >> end;
}

static uint32_t setK12(uint32_t insn, uint32_t imm) {
  return (insn & 0xffc003ff) | (extractBits(imm, 11, 0) << 10);
}

static uint32_t setK16(uint32_t insn, uint32_t imm) {
  return (insn & 0xfc0003ff) | (extractBits(imm, 15, 0) << 10);
}

static uint32_t setJ20(uint32_t insn, uint32_t imm) {
  return (insn & 0xfe00001f) | (extractBits(imm, 19, 0) << 5);
}

static uint32_t setJ5(uint32_t insn, uint32_t imm) {
  return (insn & 0xfffffc1f) | (extractBits(imm, 4, 0) << 5);
}

static uint64_t getLoongArchPage(uint64_t p)
{
    return p & ~0xfff;
}

static int apply_relocations(Filedata *filedata, void *addr)
{
    FILE *fp = filedata->fp;
    Elf64_Shdr *shdrs = filedata->shdrs;
    char *shstrtab = filedata->shstrtab;
    int shnum = filedata->ehdr.e_shnum;

    // 查找 .rela.text 重定位表
    for (int i = 0; i < shnum; i++) {
        const char *name = shstrtab + shdrs[i].sh_name;
        if (strcmp(name, ".rela.text") == 0) {
            size_t rela_count = shdrs[i].sh_size / sizeof(Elf64_Rela);
            Elf64_Rela *relas = malloc(shdrs[i].sh_size);
            if (!relas) {
                return -1;
            }

            fseek(fp, shdrs[i].sh_offset, SEEK_SET);
            fread(relas, sizeof(Elf64_Rela), rela_count, fp);

            // 应用重定位
            for (size_t j = 0; j < rela_count; j++) {
                Elf64_Rela *rela = &relas[j];

                uint64_t pc = (uint64_t)addr + rela->r_offset;
                uint64_t target_vaddr = (uint64_t)addr + rela->r_addend;
                uint32_t inst = read_inst(addr, rela->r_offset);
                uint32_t r_type = ELF64_R_TYPE(rela->r_info);

                if (r_type == R_LARCH_PCALA_HI20) {
                    uint64_t result =
                        getLoongArchPage(target_vaddr) - getLoongArchPage(pc);

                    if (target_vaddr & 0x800)
                        result += 0x1000 - 0x100000000;

                    if (result & 0x80000000)
                        result += 0x100000000;

                    inst = setJ20(inst, extractBits(result, 31, 12));
                } else if (r_type == R_LARCH_PCALA_LO12) {
                    inst = setK12(inst, extractBits(target_vaddr, 11, 0));
                } else {
                    assert(0);
                }

                write_inst((uint8_t *)addr, rela->r_offset, inst);
            }

            free(relas);
            return 0;
        }
    }

    return 0;
}

int elf_loader(const char *path, void *addr)
{
    Filedata filedata = { 0 };
    int text_size = -1;

    filedata.fp = fopen(path, "rb");
    if (!filedata.fp) {
        perror("Open file failed");
        return -1;
    }

    // 读取节区头
    if (read_section_headers(&filedata) != 0) {
        fclose(filedata.fp);
        return -1;
    }

    // 查找并加载 .text 节区
    for (int i = 0; i < filedata.ehdr.e_shnum; i++) {
        const char *name = filedata.shstrtab + filedata.shdrs[i].sh_name;
        if (strcmp(name, ".text") == 0) {
            fseek(filedata.fp, filedata.shdrs[i].sh_offset, SEEK_SET);
            fread(addr, filedata.shdrs[i].sh_size, 1, filedata.fp);
            text_size = filedata.shdrs[i].sh_size;
            break;
        }
    }

    if (text_size < 0) {
        goto cleanup;
    }

    // 应用重定位
    if (apply_relocations(&filedata, addr) != 0) {
        text_size = -1;
        goto cleanup;
    }

    // 加载符号表并建立映射
    if (load_symbols(&filedata, addr) != 0) {
        text_size = -1;
    }

cleanup:
    free(filedata.shstrtab);
    free(filedata.shdrs);
    fclose(filedata.fp);

    assert(text_size % 4 == 0);

    return text_size >> 2;
}
