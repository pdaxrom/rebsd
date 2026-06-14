main()
{
    double a, b;

    a = 1.5;
    b = a * 2.0;
    if (b > 2.9 && b < 3.1) {
        write(1, "pcc fpu ok\n", 11);
        return 0;
    }
    write(1, "pcc fpu bad\n", 12);
    return 1;
}
