extern void (*__CTOR_LIST__[])(void);
extern void (*__CTOR_END__[])(void);
extern void (*__DTOR_LIST__[])(void);
extern void (*__DTOR_END__[])(void);

void
__do_global_ctors(void)
{
    static int initialized;
    void (**p)(void);

    if (initialized)
        return;
    initialized = 1;

    for (p = __CTOR_LIST__; p < __CTOR_END__; p++)
        if (*p)
            (**p)();
}

void
__do_global_dtors(void)
{
    static int finished;
    void (**p)(void);

    if (finished)
        return;
    finished = 1;

    for (p = __DTOR_END__; p > __DTOR_LIST__;) {
        p--;
        if (*p)
            (**p)();
    }
}
