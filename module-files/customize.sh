#!/system/bin/sh
SKIPUNZIP=1
unzip -o "$ZIPFILE" 'zygisk/*' -d "$MODPATH" >&2
unzip -o "$ZIPFILE" 'module.prop' -d "$MODPATH" >&2
set_perm_recursive "$MODPATH" root root 0755 0644
[ "${ZYGISK_ENABLED:-0}" -eq 1 ] || ui_print "! 警告：Zygisk 未启用，模块不会生效"
ui_print "- 重启后生效"
