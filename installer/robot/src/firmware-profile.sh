# The installer writes a data record. Legacy installations use the original paths.
firmware_wifi_bin=/root/bin
firmware_wifi_libs=/root/lib:/lib:/usr/lib
firmware_record=$base/installer-firmware
if [ -e "$firmware_record" ] || [ -L "$firmware_record" ]; then
    [ -f "$firmware_record" ] && [ ! -L "$firmware_record" ] || return 1
    {
        IFS=' ' read -r firmware_profile firmware_wifi_bin firmware_wifi_libs || return 1
        if IFS= read -r extra || [ -n "$extra" ]; then return 1; fi
    } < "$firmware_record"
    case "$firmware_profile" in ''|*[!A-Za-z0-9_-]*) return 1;; esac
    case "$firmware_wifi_bin:$firmware_wifi_libs" in *[!A-Za-z0-9_./:-]*) return 1;; esac
    case "$firmware_wifi_bin" in /*) :;; *) return 1;; esac
    case "$firmware_wifi_libs" in /*) :;; *) return 1;; esac
fi
