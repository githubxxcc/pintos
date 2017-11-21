#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/fdtable.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"

static thread_func start_process NO_RETURN;
static bool load(const struct cmd_frame *, void (**eip) (void), void **);
static struct cmd_frame * parse_arguments(char*, const char*);
static void done_child(struct thread *);

static void zombie_destroy(struct thread *);

/* Closes a open file */
void
process_close(unsigned long fdbit)
{
    struct file_struct * fstr;
     fstr = thread_current() -> files;
    
    file_close(fstr->fdt->fd[fdbit]);
    unset_fd_bit(fstr->fdt->open_fds, fdbit);
}


/* Opens a file and returns the fd associated with that file. 
 * Returns non-negative number on non-failure, -1 on failure. 
 * Lock must be held entering the function */
int
process_open (const char * file_name)
{
    struct file * opened;

    opened = filesys_open(file_name);
    if(opened == NULL)
        return FD_ERROR;
    
    /* Allocates a new fd */
    return alloc_fd(opened); 
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *ptr, *save_ptr, *file_name_copy;
  struct cmd_frame * cf_ptr;
  tid_t tid;
  struct thread * cur, * child;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  ptr = palloc_get_page(0);
  if(ptr == NULL)
      return TID_ERROR;
  
  /* Put cmd_frame on top of the aux page */
  cf_ptr = parse_arguments(ptr, file_name);
    
  /* Break the exe name from the args */
  file_name_copy = palloc_get_page(0);
  strlcpy(file_name_copy, file_name, PGSIZE);
  file_name_copy = strtok_r(file_name_copy, " ", &save_ptr); 

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name_copy, PRI_DEFAULT, start_process, cf_ptr);

  if (tid == TID_ERROR){
      palloc_free_page(cf_ptr);  
      goto done;
  }

  /* Wait for child to be loaded */
  cur = thread_current();
  child = thread_child_tid(cur, tid);
  if(child != NULL)
    sema_down(child->loading);

  if(cur->err)
      tid = TID_ERROR;

done:
  return tid;
}

/* Utility function that takes in a pointer to store data, and 
   file_name/cmd_line to be parsed, returns a pointer to the cmd
   frame, where the layout follows: 
   struct cmd_frame 
   ... data 
*/
struct cmd_frame *
parse_arguments(char * ptr, const char * file_name) 
{
  char *parsed, *token, *tmp_cmd;
  char delim[] = " ";
  struct cmd_frame * cf_ptr;

  cf_ptr = (struct cmd_frame *)ptr;
  ptr += sizeof(struct cmd_frame);

  cf_ptr->argc = 0;
  cf_ptr->argv_len = 0;
  cf_ptr->prog_name = ptr;


  /* Copy comand line to the temp holder */
  tmp_cmd = palloc_get_page(0);
  strlcpy(tmp_cmd, file_name, PGSIZE);
  
  /* Parsed tokens */
  for(token = strtok_r(tmp_cmd,delim, &parsed); token != NULL;
          token = strtok_r(NULL, delim, &parsed))
  {
      cf_ptr->argc++;
      cf_ptr->argv_len += (strlen(token) + 1);
      strlcpy(ptr, token, strlen(token)+1);
      ptr+=(strlen(token)+1);
  }


  return cf_ptr;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *cmd_frame_)
{
  struct cmd_frame * cf = cmd_frame_;
  struct intr_frame if_;
  bool success;

  struct thread * par;
   

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (cf, &if_.eip, &if_.esp);
  
  /* If load failed, quit. */
  palloc_free_page (cf); 
    
  par = thread_current() -> parent;
  ASSERT(par != NULL);
  if (!success) {
    par->err = ERROR_LOAD;
  }

  sema_up(thread_current()->loading);

  if (!success) {
    thread_exit ();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct list_elem * e;
  struct thread * cur, * child;
  int ret;

  cur = thread_current();

  list_for_each_entry(e, cur->children)
  {
     child = list_entry(e, struct thread, parent_elem); 
     if(child->tid == child_tid){
         goto valid;
     }
  }
    
  /* No child found: process_wait() called || not a child */
  return TID_ERROR;

valid:
  /* Child exited */
  sema_down(child->exiting);

  /* Killed by the kernel */
  if(child->flags & PF_KILLED) {
      ret = TID_ERROR;
      goto done;
  }

  ret = child->exit_status;

done:
  done_child(child);
  return ret;
}


/* Free the resources of the child thread. child pointer is 
   garuanteed to be NULL after the method returns */
void
done_child(struct thread * child) 
{
    list_remove(&child->parent_elem);
    free(child->exiting);
    free(child->loading);
    free_file_struct(child->files);

    palloc_free_page(child);
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Mark the process as exiting */
  cur->flags |= PF_EXITING;

  /* Clean up the zombie children thread_struct */
  zombie_destroy (cur);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Free the thread struct resources of all the zombie children */
void
zombie_destroy(struct thread * t)
{
    struct list_elem * cur, * next;
    struct thread * child;

    for(cur = list_begin(&t->children), next = cur->next; 
            cur != list_end(&t->children);
            next = next->next) /* Not safe to use list_next() here */
    {
        child = list_entry(cur,struct thread, parent_elem); 

        if(child->status == THREAD_DYING) {
            done_child(child);
        }
        cur = next;
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const struct cmd_frame *cf);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const struct cmd_frame *cf, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char * file_name;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file_name = cf->prog_name;
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }


  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, cf))
    goto done;

  /* Prepare the user stack arguments */

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}
/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const struct cmd_frame *cf) 
{
  uint8_t *kpage;
  bool success = false;
  int argc, argv_len;
  char * stack_ptr, * argv_start;
  
  stack_ptr = PHYS_BASE;
  argc = cf->argc;
  argv_len = cf->argv_len;


  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
        //TODO: set up stack properly        

        /* Pushed argvs, need to take into account spaces */
        stack_ptr -= argv_len;
        memcpy(stack_ptr, cf->prog_name, argv_len);
        
        /* Word Align stack_ptr */
        argv_start = stack_ptr;
        stack_ptr  = (char*) ((uintptr_t)(stack_ptr) & ~0x3);
        
        /* Find the esp  */
        stack_ptr = (stack_ptr - 
                4 * (cf->argc + 4)); /* sentinel + *argv+ argc + ret */ 
        *esp = (void*)stack_ptr;

        /* Skip fake return address */
        *(int *)stack_ptr = 0;
        stack_ptr += 4;
        
        /* Set argc */
        *(int*)stack_ptr = cf->argc;
        stack_ptr += 4;

        /* Set *argv */
        *(char**)stack_ptr = stack_ptr+4;
        stack_ptr += 4;

        /* Set The rest argvs address */
        while(argc--) {
            *(void **)stack_ptr = argv_start;
            argv_start += (strlen(argv_start)+1);
            stack_ptr+=4;
        }

        /* Set zero */
        *stack_ptr = 0;


        //hex_dump((uintptr_t)(*esp), *esp, 92, true);
      }
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
