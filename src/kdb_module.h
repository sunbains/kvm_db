#ifndef _KDB_MODULE_H
#define _KDB_MODULE_H

/* Function prototypes for module initialization */
int kdb_chrdev_init(void);
void kdb_chrdev_exit(void);

/* External VM operations from vma.c */
extern const struct vm_operations_struct kdb_vm_ops;

#endif /* _KDB_MODULE_H */