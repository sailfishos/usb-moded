#!/bin/sh

PROGNAME="$(basename $0)"

# ============================================================================
# ENV
# ============================================================================

CONFIGFS_ROOT="/config"
GADGET_DIR="$CONFIGFS_ROOT/usb_gadget/g1"
CONFIG_DIR="$GADGET_DIR/configs/b.1"
FUNCTION_DIR="$GADGET_DIR/functions"
UDC_CONTROL="$GADGET_DIR/UDC"
UDC_ENABLE="$(ls /sys/class/udc)"
UDC_DISABLE=""

# "charging_only"
FUNCTION_MASS_STORAGE="mass_storage.usb0"

# "developer_mode"
FUNCTION_RNDIS="rndis_bam.rndis"

# "mtp_mode"
FUNCTION_MTP="ffs.mtp"

# ============================================================================
# LOG
# ============================================================================

LOG_LEVEL=6

log_critical() { test $LOG_LEVEL -ge 2 && echo >&2 "$PROGNAME: C: $*" ; }
log_error()    { test $LOG_LEVEL -ge 3 && echo >&2 "$PROGNAME: E: $*" ; }
log_warning()  { test $LOG_LEVEL -ge 4 && echo >&2 "$PROGNAME: W: $*" ; }
log_notice()   { test $LOG_LEVEL -ge 5 && echo >&2 "$PROGNAME: N: $*" ; }
log_info()     { test $LOG_LEVEL -ge 6 && echo >&2 "$PROGNAME: I: $*" ; }
log_debug()    { test $LOG_LEVEL -ge 7 && echo >&2 "$PROGNAME: D: $*" ; }

# ============================================================================
# CONFIGFS
# ============================================================================

configfs_validate() {
  log_debug "Validate ConfigFS mountpoint"

  mountpoint -q $CONFIGFS_ROOT || exit 1
  test -d "$GADGET_DIR"        || exit 1
  test -f "$UDC_CONTROL"       || exit 1
  test -d "$CONFIG_DIR"        || exit 1
  test -d "$FUNCTION_DIR"      || exit 1
}

# ============================================================================
# UDC
# ============================================================================

udc_control() {
  local prev="$(cat $UDC_CONTROL)"
  local next="$1"
  if [ "$prev" != "$next" ]; then
    echo > "$UDC_CONTROL" "$next"
  fi
}

udc_disable() {
  log_debug "Disable UDC"
  udc_control "$UDC_DISABLE"
}

udc_enable() {
  log_debug "Enable UDC"
  udc_control "$UDC_ENABLE"
}

# ============================================================================
# FUNCTION
# ============================================================================

function_register() {
  log_debug "Register function: $1"
  test -d "$FUNCTION_DIR/$1" || mkdir "$FUNCTION_DIR/$1"
}

function_activate() {
  log_debug "Activate function: $1"
  test -h "$CONFIG_DIR/$1" || ln -s "$FUNCTION_DIR/$1" "$CONFIG_DIR/$1"
}

function_deactivate() {
  log_debug "Deactivate function: $1"
  test -h "$CONFIG_DIR/$1" && rm "$CONFIG_DIR/$1"
}

function_deactivate_all() {
  local err=0
  for f in $CONFIG_DIR/*; do
    # symlink == function
    test -h $f || continue
    if ! function_deactivate $(basename $f); then
      err=1
    fi
  done
  return $err
}

function_list_active() {
  log_debug "List active functions"
  for f in $CONFIG_DIR/*; do
    # symlink == function
    test -h $f || continue
    log_debug "Active function: $(basename $f)"
  done
  return 0
}

function_set_attr() {
  local f="$1"
  local k="$2"
  local v="$3"
  log_debug "Function $f: set $k = $v"
  echo > "$FUNCTION_DIR/$f/$k" "$v"
}

# ============================================================================
# RNDIS
# ============================================================================

rndis_down() {
  log_debug "Disable rndis interface"
  ifconfig rndis0 down 2> /dev/null || :
}

rndis_up() {
  log_debug "Enable rndis interface"
  ifconfig rndis0 192.168.2.15 netmask 255.255.255.0  2> /dev/null || :
}

udhcpd_configure() {
  log_debug "Configure udhcpd"
  cat > /etc/udhcpd.conf <<-EOF
	start   192.168.2.1
	end     192.168.2.15
	interface       rndis0
	option  subnet  255.255.255.0
	option  lease   3600
	max_leases      15
	EOF
}

udhcpd_start() {
  log_debug "Start udhcpd"
  systemctl start udhcpd.service
}

udhcpd_stop() {
  log_debug "Stop udhcpd"
  systemctl stop udhcpd.service
}

# ============================================================================
# MTP
# ============================================================================

MTPD_ENV="LANG=en_GB.utf8 MSYNCD_LOGGING_LEVEL=7"
MTPD_BIN="/usr/lib/mtp/mtp_service"
MTPD_LOG="/tmp/mtpd.log"

mtp_mount() {
  log_debug "Mount mtp endpoint directory"
  if ! mountpoint -q /dev/mtp ; then
    /bin/mount -o uid=100000,gid=100000 -t functionfs mtp /dev/mtp
  fi
}

mtp_unmount() {
  log_debug "Unmount mtp endpoint directory"
  if  mountpoint -q /dev/mtp ; then
    /bin/umount /dev/mtp
  fi
}

mtpd_start() {
  log_debug "Start mtpd"
  (/bin/su - nemo -c "$MTPD_ENV $MTPD_BIN" 2>$MTPD_LOG) &

  local seen=0
  for i in $(seq 10); do
    sleep 0.333
    if killall -0 "$MTPD_BIN" ; then
      seen=$((seen+1))
      test "$seen" -ge 3 && return 0
    else
      log_debug "Wait for mtpd ..."
    fi
  done
  log_warning "Could not start mtpd"
  return 1
}

mtpd_stop() {
  log_debug "Stop mtpd"
  /usr/bin/killall "$MTPD_BIN" || :
}

# ============================================================================
# USB_MODE
# ============================================================================

# -- undefined --

usb_mode_enter_undefined() {
  log_debug "ENTER UNKNOWN MODE"
  # same as enter undefined...
  udc_disable
  function_deactivate_all
}

usb_mode_leave_undefined() {
  log_debug "LEAVE UNKNOWN MODE"
  udc_disable
  function_deactivate_all
}

# -- charging_only --

usb_mode_enter_charging_only() {
  log_debug "ENTER CHARGING_ONLY"
  function_register $FUNCTION_MASS_STORAGE
  function_activate $FUNCTION_MASS_STORAGE
  udc_enable
}

usb_mode_leave_charging_only() {
  log_debug "LEAVE CHARGING_ONLY"
  udc_disable
  function_deactivate_all
}

# -- developer_mode --

usb_mode_enter_developer_mode() {
  log_debug "ENTER DEVELOPER_MODE"
  function_register $FUNCTION_RNDIS
  function_set_attr $FUNCTION_RNDIS wceis 1
  function_activate $FUNCTION_RNDIS
  udc_enable
  sleep 0.5
  rndis_up
  sleep 0.5
  udhcpd_configure
  udhcpd_start
}

usb_mode_leave_developer_mode() {
  log_debug "LEAVE DEVELOPER_MODE"
  udhcpd_stop
  rndis_down
  udc_disable
  function_deactivate_all
}

# -- mtp_mode --

usb_mode_enter_mtp_mode() {
  log_debug "ENTER MTP_MODE"
  function_register $FUNCTION_MTP
  function_activate $FUNCTION_MTP
  mtp_mount
  mtpd_start
  udc_enable
}

usb_mode_leave_mtp_mode() {
  log_debug "LEAVE MTP_MODE"
  udc_disable
  mtpd_stop
  mtp_unmount
  function_deactivate_all
}

# -- mode switch --

usb_mode_enter() {
  case $1 in
  mtp_mode)
    usb_mode_enter_mtp_mode
    ;;
  developer_mode)
    usb_mode_enter_developer_mode
    ;;
  charging_only)
    usb_mode_enter_charging_only
    ;;
  undefined)
    usb_mode_enter_undefined
    ;;
  *)
    log_warning "Unknown mode: $1"
    usb_mode_enter_undefined
    ;;
  esac
}

usb_mode_leave() {
  case $1 in
  mtp_mode)
    usb_mode_leave_mtp_mode
    ;;
  developer_mode)
    usb_mode_leave_developer_mode
    ;;
  charging_only)
    usb_mode_leave_charging_only
    ;;
  undefined)
    usb_mode_leave_undefined
    ;;
  *)
    log_warning "Unknown mode: $1"
    usb_mode_leave_undefined
    ;;
  esac
}

usb_mode_switch() {
  configfs_validate
  local next="$1"
  local curr="$(cat /tmp/usb-mode)"
  test ! -z "$curr" || curr="undefined"

  if [ "$curr" != "$next" ]; then
    log_info "SWITCH FROM: $curr TO: $next"
    usb_mode_leave "$curr"
    echo > /tmp/usb-mode "$next"
    usb_mode_enter "$next"
  else
    log_info "ALREADY IN: $next"
  fi
}

# ============================================================================
# MAIN
# ============================================================================

# try to guess if we were executed or sourced
if [ "$PROGNAME" = "set_usb_mode.sh" ]; then
  TARGET_MODE="$1"
  case $TARGET_MODE in
  cha*)
    TARGET_MODE="charging_only"
    ;;
  dev*)
    TARGET_MODE="developer_mode"
    ;;
  mtp*)
    TARGET_MODE="mtp_mode"
    ;;
  und*)
    TARGET_MODE="undefined"
    ;;
  -v|--verbose)
    LOG_LEVEL=$((LOG_LEVEL+1))
    ;;
  -q|--quiet)
    LOG_LEVEL=$((LOG_LEVEL-1))
    ;;
  -s|--silent)
    LOG_LEVEL=0
    ;;
  -h|--help|--usage)
    cat <<-EOF
	Usage:
	  $PROGNAME <mode>

	Valid modes are:
	  - charging_only
	  - developer_mode
	  - mtp_mode
	  - undefined
	EOF
    exit 0
    ;;
  *)
    cat >&2 <<-EOF
	unknown option: ${TARGET_MODE:-<nothing>}
	(Use $PROGNAME --usage to show help)
	EOF
    exit 1
    ;;
  esac
  usb_mode_switch "$TARGET_MODE"
fi
