# Explicit upstream selection; generated sources are listed separately.

NCURSES_BASE_SOURCES := \
	lib_addch.c lib_addstr.c lib_beep.c lib_bkgd.c lib_box.c \
	lib_chgat.c lib_clear.c lib_clearok.c lib_clrbot.c lib_clreol.c \
	lib_color.c lib_colorset.c lib_delch.c lib_delwin.c lib_dft_fgbg.c \
	lib_echo.c lib_endwin.c lib_erase.c lib_flash.c lib_freeall.c \
	lib_getch.c lib_getstr.c lib_hline.c lib_immedok.c lib_inchstr.c \
	lib_initscr.c lib_insch.c lib_insdel.c lib_insnstr.c lib_instr.c \
	lib_isendwin.c lib_leaveok.c key_defined.c keybound.c keyok.c \
	lib_mouse.c lib_move.c lib_mvwin.c lib_newterm.c lib_newwin.c \
	lib_nl.c lib_overlay.c lib_pad.c lib_printw.c lib_redrawln.c \
	lib_refresh.c lib_restart.c lib_scanw.c lib_screen.c lib_scroll.c \
	lib_scrollok.c lib_scrreg.c lib_set_term.c lib_slk.c lib_slkatr_set.c \
	lib_slkatrof.c lib_slkatron.c lib_slkatrset.c lib_slkattr.c lib_slkclear.c \
	lib_slkcolor.c lib_slkinit.c lib_slklab.c lib_slkrefr.c lib_slkset.c \
	lib_slktouch.c lib_touch.c lib_ungetch.c version.c lib_vline.c \
	lib_wattroff.c lib_wattron.c lib_winch.c lib_window.c nc_panel.c \
	safe_sprintf.c tries.c wresize.c

NCURSES_TTY_SOURCES := \
	hardscroll.c hashmap.c lib_mvcur.c lib_tstp.c lib_twait.c \
	lib_vidattr.c tty_update.c

NCURSES_TINFO_SOURCES := \
	access.c add_tries.c alloc_entry.c alloc_ttype.c captoinfo.c \
	comp_error.c comp_expand.c comp_hash.c comp_parse.c comp_scan.c \
	db_iterator.c doalloc.c entries.c free_ttype.c getenv_num.c \
	hashed_db.c home_terminfo.c init_keytry.c lib_acs.c lib_baudrate.c \
	lib_cur_term.c lib_data.c lib_has_cap.c lib_kernel.c lib_longname.c \
	lib_napms.c lib_options.c lib_print.c lib_raw.c lib_setup.c \
	lib_termcap.c lib_termname.c lib_tgoto.c lib_ti.c lib_tparm.c \
	lib_tputs.c lib_ttyflags.c name_match.c obsolete.c parse_entry.c \
	read_entry.c read_termcap.c strings.c trim_sgr0.c use_screen.c \
	write_entry.c

NCURSES_TRACE_SOURCES := \
	lib_trace.c visbuf.c
