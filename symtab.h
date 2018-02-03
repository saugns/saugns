struct SGSSymtab;
typedef struct SGSSymtab SGSSymtab;

SGSSymtab* SGS_symtab_create(void);
void SGS_symtab_destroy(SGSSymtab *o);

void* SGS_symtab_get(SGSSymtab *o, const char *key);
void* SGS_symtab_set(SGSSymtab *o, const char *key, void *value);
