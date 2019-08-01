# Extra header tests
include $(src)/Makefile.header-test

i915-$(CONFIG_DEBUG_FS) += \
	intel_pipe_crc.o

# modesetting core code
i915-y += \
	intel_atomic.o \
	intel_atomic_plane.o \
	intel_audio.o \
	intel_bios.o \
	intel_bw.o \
	intel_cdclk.o \
	intel_color.o \
	intel_combo_phy.o \
	intel_connector.o \
	intel_display.o \
	intel_display_power.o \
	intel_dpio_phy.o \
	intel_dpll_mgr.o \
	intel_fbc.o \
	intel_fifo_underrun.o \
	intel_frontbuffer.o \
	intel_hdcp.o \
	intel_hotplug.o \
	intel_lpe_audio.o \
	intel_overlay.o \
	intel_psr.o \
	intel_quirks.o \
	intel_sprite.o
i915-$(CONFIG_ACPI) += \
	intel_acpi.o \
	intel_opregion.o
i915-$(CONFIG_DRM_FBDEV_EMULATION) += \
	intel_fbdev.o

# modesetting output/encoder code
i915-y += \
	dvo_ch7017.o \
	dvo_ch7xxx.o \
	dvo_ivch.o \
	dvo_ns2501.o \
	dvo_sil164.o \
	dvo_tfp410.o \
	icl_dsi.o \
	intel_crt.o \
	intel_ddi.o \
	intel_dp.o \
	intel_dp_aux_backlight.o \
	intel_dp_link_training.o \
	intel_dp_mst.o \
	intel_dsi.o \
	intel_dsi_dcs_backlight.o \
	intel_dsi_vbt.o \
	intel_dvo.o \
	intel_gmbus.o \
	intel_hdmi.o \
	intel_lspcon.o \
	intel_lvds.o \
	intel_panel.o \
	intel_sdvo.o \
	intel_tv.o \
	intel_vdsc.o \
	vlv_dsi.o \
	vlv_dsi_pll.o
