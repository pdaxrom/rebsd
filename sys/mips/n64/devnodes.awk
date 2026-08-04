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

function emit_pty_nodes(i, n) {
    if (pty_enabled == "")
        return
    if (pty_nunits == "")
        require_value("PTY_NUNITS")
    n = pty_nunits + 0
    require_value("N64_PTS_MAJOR")
    require_value("N64_PTC_MAJOR")
    for (i = 0; i < n; i++) {
        emit_node("cdev", "/dev/ttyp" i, defs["N64_PTS_MAJOR"], i, "0666")
        emit_node("cdev", "/dev/ptyp" i, defs["N64_PTC_MAJOR"], i, "0666")
    }
}

function emit_ramdisk_nodes(i, n) {
    require_value("MIPS_RAMDISK_MAJOR")
    require_value("MIPS_RAMDISK_FIRST_MINOR")
    require_value("RAMDISK_MAX_DEVICES")
    n = defs["RAMDISK_MAX_DEVICES"] + 0
    for (i = 0; i < n; i++)
        emit_node("bdev", "/dev/ram" i,
            defs["MIPS_RAMDISK_MAJOR"],
            defs["MIPS_RAMDISK_FIRST_MINOR"] + i, "")
}

function emit_input_nodes(i) {
    require_value("N64_JOYPAD_MAJOR")
    require_value("N64_MOUSE_MAJOR")
    require_value("N64_KBD_MAJOR")
    for (i = 0; i < 4; i++) {
        emit_node("cdev", "/dev/joypad" i, defs["N64_JOYPAD_MAJOR"], i, "0666")
        emit_node("cdev", "/dev/mouse" i, defs["N64_MOUSE_MAJOR"], i, "0666")
        emit_node("cdev", "/dev/kbd" i, defs["N64_KBD_MAJOR"], i, "0666")
    }
}

$1 == "#define" {
    define_value($2, $3)
}

END {
    defs["CONS_MAJOR"] = cons_major
    defs["CONS_MINOR"] = cons_minor

    require_value("N64_ROMDISK_MAJOR")
    require_value("N64_ROMDISK_ROOT_MINOR")
    require_value("MIPS_RAMDISK_MAJOR")
    require_value("N64_TTY_MAJOR")
    require_value("N64_SERIAL_MAJOR")
    require_value("N64_RGBLED_MAJOR")
    require_value("N64_CARTFLASH_MAJOR")
    require_value("N64_FB_MAJOR")
    require_value("MEM_MAJOR")
    require_value("CONS_MAJOR")
    require_value("CONS_MINOR")

    print "#"
    print "# Generated from N64 kernel device definitions."
    print "#"
    emit_node("bdev", "/dev/romdisk",
        defs["N64_ROMDISK_MAJOR"], defs["N64_ROMDISK_ROOT_MINOR"], "")
    emit_ramdisk_nodes()
    emit_node("cdev", "/dev/console",
        defs["CONS_MAJOR"], defs["CONS_MINOR"], "")
    emit_node("cdev", "/dev/tty", defs["N64_TTY_MAJOR"], 0, "")
    emit_node("cdev", "/dev/ttyS0", defs["N64_SERIAL_MAJOR"], 0, "")
    emit_node("cdev", "/dev/rgbled0", defs["N64_RGBLED_MAJOR"], 0, "")
    emit_node("cdev", "/dev/cartflash0",
        defs["N64_CARTFLASH_MAJOR"], 0, "0600")
    emit_node("cdev", "/dev/fb0", defs["N64_FB_MAJOR"], 0, "0666")
    emit_input_nodes()
    emit_pty_nodes()
    emit_node("cdev", "/dev/mem", defs["MEM_MAJOR"], 0, "0640")
    emit_node("cdev", "/dev/kmem", defs["MEM_MAJOR"], 1, "0640")
    emit_node("cdev", "/dev/null", defs["MEM_MAJOR"], 2, "0666")
    emit_node("cdev", "/dev/zero", defs["MEM_MAJOR"], 3, "0666")
}
