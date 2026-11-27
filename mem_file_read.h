#ifndef DFM_MEMORY_FILE_READ_H
#define DFM_MEMORY_FILE_READ_H

#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/limits.h>

/* Adapted from Qdrive memory_access/readonly_file.h (Apache-2.0).
 * Reclaimed MAP_SHARED read-only metadata has no swap entry. Read its pinned
 * backing file without reopening a path or faulting the target address in.
 * Private/COW and potentially writable mappings must never take this route.
 */
static bool mem_file_range(struct vm_area_struct *vma, unsigned long address,
                           size_t length, struct file **file, loff_t *offset)
{
    u64 base, delta;

    if (!vma || address < vma->vm_start || length > vma->vm_end - address ||
        !(vma->vm_flags & VM_READ) || !(vma->vm_flags & VM_MAYSHARE) ||
        (vma->vm_flags & (VM_MAYWRITE | VM_WRITE | VM_IO | VM_PFNMAP |
                          VM_MIXEDMAP | VM_HUGETLB)) ||
        !vma->vm_file || !S_ISREG(file_inode(vma->vm_file)->i_mode) ||
        !(vma->vm_file->f_mode & FMODE_READ))
        return false;
    if ((u64)vma->vm_pgoff > ((u64)LLONG_MAX >> PAGE_SHIFT))
        return false;
    base = (u64)vma->vm_pgoff << PAGE_SHIFT;
    delta = address - vma->vm_start;
    if (delta > (u64)LLONG_MAX - base ||
        length > (u64)LLONG_MAX - base - delta)
        return false;
    *file = vma->vm_file;
    *offset = base + delta;
    return *offset + length <= i_size_read(file_inode(*file));
}

static long mem_file_copy(struct mm_struct *mm, unsigned long address,
                          void *output, size_t count, bool user_output)
{
    struct file *file = NULL, *check_file = NULL;
    const struct cred *saved;
    loff_t offset = 0, check_offset = 0, cursor, size;
    unsigned long start = untagged_addr(address);
    char *buffer;
    size_t done = 0;
    long ret = -EOPNOTSUPP;

    /* Only small metadata requests; the ordinary reader already checked the
     * source range and destination. No partial file result is published. */
    if (!count || count > 4096)
        return ret;
    mmap_read_lock(mm);
    if (mem_file_range(find_vma(mm, start), start, count, &file, &offset))
        get_file(file);
    else
        file = NULL;
    mmap_read_unlock(mm);
    if (!file)
        return ret;
    buffer = kmalloc(count, GFP_KERNEL);
    if (!buffer) { ret = -ENOMEM; goto release; }
    size = i_size_read(file_inode(file));
    cursor = offset;
    /* Same opening-credential/VFS contract as the existing target-fd query.
     * A private cursor leaves the target open description's position intact. */
    saved = override_creds(file->f_cred);
    while (done < count) {
        ssize_t got = kernel_read(file, buffer + done, count - done, &cursor);
        if (got <= 0) {
            ret = got < 0 ? got : -EIO;
            break;
        }
        done += got;
    }
    revert_creds(saved);
    if (done != count)
        goto free_buffer;
    mmap_read_lock(mm);
    if (!mem_file_range(find_vma(mm, start), start, count, &check_file, &check_offset) ||
        check_file != file || check_offset != offset ||
        size != i_size_read(file_inode(file)))
        ret = -EAGAIN;
    else
        ret = 0;
    mmap_read_unlock(mm);
    if (!ret) {
        if (user_output) {
            if (copy_to_user((void __user *)output, buffer, count)) ret = -EFAULT;
        } else {
            memcpy(output, buffer, count);
        }
    }
free_buffer:
    kfree(buffer);
release:
    fput(file);
    return ret;
}

static long mem_read_range(struct mm_struct *mm, unsigned long address,
                           void *output, size_t count, bool user_output, bool clean)
{
    long ret = mem_copy_range(mm, address, output, count, user_output, clean);

    if (ret == -EFAULT) {
        long file_ret = mem_file_copy(mm, address, output, count, user_output);
        if (file_ret != -EOPNOTSUPP) ret = file_ret;
    }
    return ret;
}

#endif
