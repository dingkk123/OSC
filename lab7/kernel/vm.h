#ifndef VM_H
#define VM_H

#define PAGE_OFFSET 0xffffffc000000000UL
#define PAGE_SIZE   (1UL << 12) // 4KB
#define PMD_SIZE    (1UL << 21) // one entry size in PGD level, 2MB
#define PGD_SIZE    (1UL << 30) // one entry size in PGD level, 1GB

#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)
#define PTE_COW (1UL << 8)

#define PROT_KERNEL (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PROT_MMIO   (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PROT_USER_RX  (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RW  (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)

static inline unsigned long phys_to_virt(unsigned long pa) {
    return pa + PAGE_OFFSET;
}

static inline unsigned long virt_to_phys_addr(unsigned long va) {
    if (va >= PAGE_OFFSET)
        return va - PAGE_OFFSET;
    return va;
}

static inline unsigned long virt_to_phys_ptr(const void *ptr) {
    return virt_to_phys_addr((unsigned long)ptr);
}

void setup_vm(void);
void drop_identity_map(void);
unsigned long *vm_create_pgd(void);
void vm_destroy_pgd(unsigned long *pgd);
void vm_switch_pgd(unsigned long *pgd);
void map_pages(unsigned long *pgd,
               unsigned long va,
               unsigned long size,
               unsigned long pa,
               unsigned long prot);
int vm_get_mapping(unsigned long *pgd,
                   unsigned long va,
                   unsigned long *pa_out,
                   unsigned long *flags_out);
void vm_free_user_pages(unsigned long *pgd, unsigned long va, unsigned long size);
int vm_share_region_cow(unsigned long *parent_pgd,
                        unsigned long *child_pgd,
                        unsigned long va,
                        unsigned long size);
int vm_handle_cow_fault(unsigned long *pgd, unsigned long va);

#endif

