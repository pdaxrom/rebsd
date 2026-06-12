function define_value(name, value) {
    if (value ~ /^[0-9]+$/)
        defs[name] = value
}

function require_value(name) {
    if (!(name in defs) || defs[name] == "") {
        printf("missing %s\n", name) > "/dev/stderr"
        exit 1
    }
}

function emit_node(type, path, major, minor, mode) {
    print type " " path
    print "major " major
    print "minor " minor
    if (mode != "")
        print "mode " mode
    print ""
}

$1 == "#define" {
    define_value($2, $3)
}

END {
    defs["CONS_MAJOR"] = cons_major
    defs["CONS_MINOR"] = cons_minor

    require_value("N64_ROMDISK_MAJOR")
    require_value("N64_ROMDISK_ROOT_MINOR")
    require_value("N64_RAMSWAP_MAJOR")
    require_value("N64_RAMSWAP_MINOR")
    require_value("MEM_MAJOR")
    require_value("CONS_MAJOR")
    require_value("CONS_MINOR")

    print "#"
    print "# Generated from N64 kernel device definitions."
    print "#"
    emit_node("bdev", "/dev/romdisk",
        defs["N64_ROMDISK_MAJOR"], defs["N64_ROMDISK_ROOT_MINOR"], "")
    emit_node("bdev", "/dev/swap",
        defs["N64_RAMSWAP_MAJOR"], defs["N64_RAMSWAP_MINOR"], "")
    emit_node("cdev", "/dev/console",
        defs["CONS_MAJOR"], defs["CONS_MINOR"], "")
    emit_node("cdev", "/dev/null", defs["MEM_MAJOR"], 2, 666)
    emit_node("cdev", "/dev/zero", defs["MEM_MAJOR"], 3, 666)
}
