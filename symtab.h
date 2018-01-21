struct SGSSymtab;
typedef struct SGSSymtab SGSSymtab;

SGSSymtab* SGSSymtab_create(void);
void SGSSymtab_destroy(SGSSymtab *o);

void* SGSSymtab_get(SGSSymtab *o, const char *key);
void* SGSSymtab_set(SGSSymtab *o, const char *key, void *value);
