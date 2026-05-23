#include "vm.h"
#include "allocate.h"
#include "utils.h"

#define SATP_SV39         (8UL << 60)
#define MAKE_SATP(pgd_pa) (SATP_SV39 | (virt_to_phys_addr((unsigned long)(pgd_pa)) >> 12))

#define PGD_SHIFT 30
#define PMD_SHIFT 21
#define PTE_SHIFT 12

#define ENTRIES_PER_TABLE 512
#define LINEAR_MAP_GIB 4
#define LOW_PGD_INDEX 0
#define KERNEL_PGD_INDEX ((PAGE_OFFSET >> PGD_SHIFT) & 0x1ff)

#define VPN(va, level) (((va) >> (PTE_SHIFT + 9 * (level))) & 0x1ff)
#define MAKE_PTE(pa, flags) ((((unsigned long)(pa) >> PTE_SHIFT) << 10) | (flags))
#define PTE_TO_PA(pte) (((pte) >> 10) << PTE_SHIFT)
#define PTE_FLAGS(pte) ((pte) & 0x3ffUL)
#define PTE_IS_LEAF(pte) ((pte) & (PTE_R | PTE_W | PTE_X))

#define EARLY_PTE_TABLES 640

#define MMIO_BASE 0xc0000000UL
#define MMIO_SIZE 0x40000000UL

#define FRAMEBUFFER_BASE 0x7f700000UL
#define FRAMEBUFFER_SIZE (1920UL * 1080UL * 4UL)

#define SSTATUS_SUM (1UL << 18)

static unsigned long __attribute__((section(".boot_pgtable"), aligned(PAGE_SIZE)))
    kernel_pgd[ENTRIES_PER_TABLE];

static unsigned long __attribute__((section(".boot_pgtable"), aligned(PAGE_SIZE)))
    kernel_pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE];

static unsigned long __attribute__((section(".boot_pgtable"), aligned(PAGE_SIZE)))
    early_pte_tables[EARLY_PTE_TABLES][ENTRIES_PER_TABLE];

static unsigned long early_pte_next;
static int vm_runtime_ready;

static void clear_table(unsigned long *table) {
    for (unsigned long i = 0; i < ENTRIES_PER_TABLE; i++)
        table[i] = 0;
}

static unsigned long *alloc_early_pte_table(void) {
    if (early_pte_next >= EARLY_PTE_TABLES)
        while (1)
            ;

    return early_pte_tables[early_pte_next++];
}

static unsigned long *pa_to_table_ptr(unsigned long pa) {
    if (vm_runtime_ready)
        return (unsigned long *)phys_to_virt(pa);
    return (unsigned long *)pa;
}

static unsigned long *alloc_runtime_table(void) {
    unsigned long *table = alloc_page();

    if (table == 0)
        while (1)
            ;

    clear_table(table);
    return table;
}

static unsigned long *alloc_page_table(void) {
    if (vm_runtime_ready)
        return alloc_runtime_table();
    return alloc_early_pte_table();
}

static unsigned long *ensure_pmd_table(unsigned long *pgd, unsigned long va) {
    unsigned long idx = VPN(va, 2);
    unsigned long entry = pgd[idx];
    unsigned long *pmd;

    if ((entry & PTE_V) && !PTE_IS_LEAF(entry))
        return pa_to_table_ptr(PTE_TO_PA(entry));

    pmd = alloc_page_table();
    clear_table(pmd);
    pgd[idx] = MAKE_PTE(virt_to_phys_ptr(pmd), PTE_V);
    return pmd;
}

static unsigned long *ensure_pte_table(unsigned long *pgd, unsigned long va) {
    unsigned long *pmd;
    unsigned long *pte;
    unsigned long entry;
    unsigned long old_pa;
    unsigned long old_flags;
    unsigned long idx;

    pmd = ensure_pmd_table(pgd, va);
    idx = VPN(va, 1);
    entry = pmd[idx];

    if ((entry & PTE_V) && !PTE_IS_LEAF(entry))
        return pa_to_table_ptr(PTE_TO_PA(entry));

    pte = alloc_page_table();
    clear_table(pte);

    if (entry & PTE_V) {
        old_pa = PTE_TO_PA(entry);
        old_flags = PTE_FLAGS(entry);
        for (unsigned long i = 0; i < ENTRIES_PER_TABLE; i++)
            pte[i] = MAKE_PTE(old_pa + i * PAGE_SIZE, old_flags);
    }

    pmd[idx] = MAKE_PTE(virt_to_phys_ptr(pte), PTE_V);
    return pte;
}

static void map_page_4k(unsigned long *pgd,
                        unsigned long va,
                        unsigned long pa,
                        unsigned long flags) {
    unsigned long *pte = ensure_pte_table(pgd, va);

    pte[VPN(va, 0)] = MAKE_PTE(pa, flags);
}

static void map_pages_4k(unsigned long *pgd,
                         unsigned long va,
                         unsigned long pa,
                         unsigned long size,
                         unsigned long flags) {
    unsigned long start;
    unsigned long offset;
    unsigned long end;
    unsigned long limit;

    if (size == 0 || va + size < va)
        return;

    limit = va + size;
    if (limit > ~0UL - (PAGE_SIZE - 1))
        return;

    start = va & ~(PAGE_SIZE - 1);
    offset = va - start;
    if (pa < offset)
        return;

    end = (limit + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    pa -= offset;
    for (unsigned long cur = start; cur < end; cur += PAGE_SIZE, pa += PAGE_SIZE)
        map_page_4k(pgd, cur, pa, flags);
}

void map_pages(unsigned long *pgd,
               unsigned long va,
               unsigned long size,
               unsigned long pa,
               unsigned long prot) {
    if (pgd == 0)
        pgd = kernel_pgd;

    map_pages_4k(pgd, va, pa, size, prot);
    asm volatile("sfence.vma zero, zero" : : : "memory");
}

void vm_map_pages(unsigned long va,
                  unsigned long pa,
                  unsigned long size,
                  unsigned long prot) {
    map_pages_4k(kernel_pgd, va, pa, size, prot);
    asm volatile("sfence.vma zero, zero" : : : "memory");
}

unsigned long vm_translate(unsigned long *pgd, unsigned long va) {
    unsigned long entry;
    unsigned long pa;
    unsigned long *table = pgd;

    for (int level = 2; level > 0; level--) {
        entry = table[VPN(va, level)];
        if ((entry & PTE_V) == 0)
            return 0;
        if (PTE_IS_LEAF(entry)) {
            pa = PTE_TO_PA(entry);
            if (level == 2)
                return pa + (va & (PGD_SIZE - 1));
            return pa + (va & (PMD_SIZE - 1));
        }
        table = pa_to_table_ptr(PTE_TO_PA(entry));
    }

    entry = table[VPN(va, 0)];
    if ((entry & PTE_V) == 0 || !PTE_IS_LEAF(entry))
        return 0;

    return PTE_TO_PA(entry) + (va & (PAGE_SIZE - 1));
}

int vm_get_mapping(unsigned long *pgd,
                   unsigned long va,
                   unsigned long *pa_out,
                   unsigned long *flags_out) {
    unsigned long entry;
    unsigned long pa;
    unsigned long *table = pgd;

    if (pgd == 0)
        return 0;

    for (int level = 2; level > 0; level--) {
        entry = table[VPN(va, level)];
        if ((entry & PTE_V) == 0)
            return 0;
        if (PTE_IS_LEAF(entry)) {
            pa = PTE_TO_PA(entry);
            if (pa_out) {
                if (level == 2)
                    *pa_out = pa + (va & (PGD_SIZE - 1));
                else
                    *pa_out = pa + (va & (PMD_SIZE - 1));
            }
            if (flags_out)
                *flags_out = PTE_FLAGS(entry);
            return 1;
        }
        table = pa_to_table_ptr(PTE_TO_PA(entry));
    }

    entry = table[VPN(va, 0)];
    if ((entry & PTE_V) == 0 || !PTE_IS_LEAF(entry))
        return 0;

    if (pa_out)
        *pa_out = PTE_TO_PA(entry) + (va & (PAGE_SIZE - 1));
    if (flags_out)
        *flags_out = PTE_FLAGS(entry);
    return 1;
}

static unsigned long *get_l0_pte(unsigned long *pgd, unsigned long va) {
    unsigned long entry;
    unsigned long *table = pgd;

    if (pgd == 0)
        return 0;

    for (int level = 2; level > 0; level--) {
        entry = table[VPN(va, level)];
        if ((entry & PTE_V) == 0 || PTE_IS_LEAF(entry))
            return 0;
        table = pa_to_table_ptr(PTE_TO_PA(entry));
    }

    return &table[VPN(va, 0)];
}

void vm_free_user_pages(unsigned long *pgd, unsigned long va, unsigned long size) {
    unsigned long start;
    unsigned long end;
    unsigned long limit;

    if (pgd == 0 || size == 0)
        return;
    if (va + size < va)
        return;

    limit = va + size;
    if (limit > ~0UL - (PAGE_SIZE - 1))
        return;

    start = va & ~(PAGE_SIZE - 1);
    end = (limit + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (unsigned long cur = start; cur < end; cur += PAGE_SIZE) {
        unsigned long *pte = get_l0_pte(pgd, cur);

        if (pte == 0 || (*pte & PTE_V) == 0 || !PTE_IS_LEAF(*pte))
            continue;

        if (*pte & PTE_U) {
            unsigned long pa = PTE_TO_PA(*pte);
            if (pa)
                free((void *)phys_to_virt(pa));
        }

        *pte = 0;
    }

    asm volatile("sfence.vma zero, zero" : : : "memory");
}

int vm_share_region_cow(unsigned long *parent_pgd,
                        unsigned long *child_pgd,
                        unsigned long va,
                        unsigned long size) {
    unsigned long start;
    unsigned long end;
    unsigned long limit;

    if (parent_pgd == 0 || child_pgd == 0 || size == 0)
        return 0;
    if (va + size < va)
        return -1;

    limit = va + size;
    if (limit > ~0UL - (PAGE_SIZE - 1))
        return -1;

    start = va & ~(PAGE_SIZE - 1);
    end = (limit + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (unsigned long cur = start; cur < end; cur += PAGE_SIZE) {
        unsigned long *pte = get_l0_pte(parent_pgd, cur);
        unsigned long flags;
        unsigned long pa;

        if (pte == 0 || (*pte & PTE_V) == 0 || !PTE_IS_LEAF(*pte))
            continue;

        flags = PTE_FLAGS(*pte);
        if ((flags & PTE_U) == 0)
            continue;

        pa = PTE_TO_PA(*pte);

        if (flags & PTE_W) {
            flags &= ~PTE_W;
            flags |= PTE_COW;
            *pte = MAKE_PTE(pa, flags);
        }

        if (page_ref_inc_pa(pa) < 0)
            return -1;

        map_page_4k(child_pgd, cur, pa, flags);
    }

    asm volatile("sfence.vma zero, zero" : : : "memory");
    return 0;
}

int vm_handle_cow_fault(unsigned long *pgd, unsigned long va) {
    unsigned long page = va & ~(PAGE_SIZE - 1);
    unsigned long *pte = get_l0_pte(pgd, page);
    unsigned long flags;
    unsigned long old_pa;
    unsigned long new_flags;
    int refcount;
    void *new_page;

    if (pte == 0 || (*pte & PTE_V) == 0 || !PTE_IS_LEAF(*pte))
        return 0;

    flags = PTE_FLAGS(*pte);
    if ((flags & PTE_COW) == 0 || (flags & PTE_U) == 0)
        return 0;

    old_pa = PTE_TO_PA(*pte);
    new_flags = (flags | PTE_W) & ~PTE_COW;
    refcount = page_ref_count_pa(old_pa);

    if (refcount <= 0)
        return -1;

    if (refcount <= 1) {
        *pte = MAKE_PTE(old_pa, new_flags);
        asm volatile("sfence.vma zero, zero" : : : "memory");
        return 1;
    }

    new_page = allocate(PAGE_SIZE);
    if (new_page == 0)
        return -1;

    kmemcpy_local(new_page, (void *)phys_to_virt(old_pa), PAGE_SIZE);
    free((void *)phys_to_virt(old_pa));
    *pte = MAKE_PTE(virt_to_phys_ptr(new_page), new_flags);

    asm volatile("sfence.vma zero, zero" : : : "memory");
    return 1;
}

unsigned long *vm_create_pgd(void) {
    unsigned long *pgd = alloc_runtime_table();

    for (unsigned long i = KERNEL_PGD_INDEX; i < ENTRIES_PER_TABLE; i++)
        pgd[i] = kernel_pgd[i];

    return pgd;
}

void vm_destroy_pgd(unsigned long *pgd) {
    if (pgd == 0 || pgd == kernel_pgd)
        return;

    for (unsigned long i = 0; i < KERNEL_PGD_INDEX; i++) {
        unsigned long pmd_entry = pgd[i];
        unsigned long *pmd;

        if ((pmd_entry & PTE_V) == 0)
            continue;
        if (PTE_IS_LEAF(pmd_entry))
            continue;

        pmd = pa_to_table_ptr(PTE_TO_PA(pmd_entry));
        for (unsigned long j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pte_entry = pmd[j];

            if ((pte_entry & PTE_V) == 0)
                continue;
            if (!PTE_IS_LEAF(pte_entry))
                free(pa_to_table_ptr(PTE_TO_PA(pte_entry)));
        }

        free(pmd);
    }

    free(pgd);
}

void vm_switch_pgd(unsigned long *pgd) {
    if (pgd == 0)
        pgd = kernel_pgd;

    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(pgd))
        : "memory");
}

void setup_vm(void) {
    vm_runtime_ready = 0;
    early_pte_next = 0;
    clear_table(kernel_pgd);

    for (unsigned long i = 0; i < LINEAR_MAP_GIB; i++) {
        clear_table(kernel_pmd[i]);

        kernel_pgd[LOW_PGD_INDEX + i] =
            MAKE_PTE(virt_to_phys_ptr(kernel_pmd[i]), PTE_V);
        kernel_pgd[KERNEL_PGD_INDEX + i] =
            MAKE_PTE(virt_to_phys_ptr(kernel_pmd[i]), PTE_V);

        for (unsigned long j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = i * PGD_SIZE + j * PMD_SIZE;
            kernel_pmd[i][j] = MAKE_PTE(pa, PROT_KERNEL);
        }
    }

    map_pages_4k(kernel_pgd, MMIO_BASE, MMIO_BASE, MMIO_SIZE, PROT_MMIO);
    map_pages_4k(kernel_pgd, FRAMEBUFFER_BASE, FRAMEBUFFER_BASE, FRAMEBUFFER_SIZE, PROT_MMIO);

    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(kernel_pgd))
        : "memory");
}

void drop_identity_map(void) {
    vm_runtime_ready = 1;

    for (unsigned long i = 0; i < LINEAR_MAP_GIB; i++)
        kernel_pgd[LOW_PGD_INDEX + i] = 0;

    asm volatile(
        "sfence.vma zero, zero\n"
        "csrs sstatus, %0\n"
        :
        : "r"(SSTATUS_SUM)
        : "memory");
}

