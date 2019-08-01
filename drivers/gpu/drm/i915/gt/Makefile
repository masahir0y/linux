# Extra header tests
include $(src)/Makefile.header-test

i915-y += \
	intel_breadcrumbs.o \
	intel_context.o \
	intel_engine_cs.o \
	intel_engine_pm.o \
	intel_gt_pm.o \
	intel_hangcheck.o \
	intel_lrc.o \
	intel_reset.o \
	intel_ringbuffer.o \
	intel_mocs.o \
	intel_sseu.o \
	intel_workarounds.o
i915-$(CONFIG_DRM_I915_SELFTEST) += \
	mock_engine.o
