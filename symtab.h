struct MGSSymtab;
typedef struct MGSSymtab MGSSymtab;

MGSSymtab* MGSSymtab_create(void);
void MGSSymtab_destroy(MGSSymtab *o);

void* MGSSymtab_get(MGSSymtab *o, const char *key);
void* MGSSymtab_set(MGSSymtab *o, const char *key, void *value);
