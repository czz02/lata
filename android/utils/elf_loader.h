#ifndef ANDROID_UTILS_ELF_LOADER_H
#define ANDROID_UTILS_ELF_LOADER_H

int elf_loader(const char *path, void *addr);

#ifndef R_LARCH_PCALA_HI20
#define R_LARCH_PCALA_HI20 71
#endif
#ifndef R_LARCH_PCALA_LO12
#define R_LARCH_PCALA_LO12 72
#endif

#endif
