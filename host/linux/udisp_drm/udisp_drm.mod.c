#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_MITIGATION_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x7c656756, "drm_open" },
	{ 0xefc4d458, "drm_poll" },
	{ 0xaddc4c80, "drm_gem_fb_end_cpu_access" },
	{ 0x49cd25ed, "alloc_workqueue" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0xd8b29095, "drm_mode_probed_add" },
	{ 0x2c462969, "drm_gem_shmem_dumb_create" },
	{ 0x742f10a4, "drm_atomic_helper_shutdown" },
	{ 0xc5ba6ece, "usb_register_driver" },
	{ 0xb7426374, "drm_mode_object_get" },
	{ 0x69acdf38, "memcpy" },
	{ 0x37a0cba, "kfree" },
	{ 0xb43bee2a, "drmm_mode_config_init" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xf1b69c5b, "drm_mode_object_put" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0x122c3a7e, "_printk" },
	{ 0x1d24c881, "___ratelimit" },
	{ 0x946d14d5, "__drmm_add_action_or_reset" },
	{ 0x3ff4b298, "usb_put_dev" },
	{ 0xf78074be, "usb_bulk_msg" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x87c437f4, "drm_atomic_helper_commit" },
	{ 0xf0e369c2, "drm_atomic_helper_check" },
	{ 0x62c5fd7b, "drm_atomic_helper_connector_destroy_state" },
	{ 0xa916b694, "strnlen" },
	{ 0x81bce1fe, "usb_get_dev" },
	{ 0xc01fbb8d, "drm_gem_mmap" },
	{ 0xbf756e3b, "drm_mode_create" },
	{ 0xe4d37e22, "_dev_info" },
	{ 0xacad7dff, "drm_ioctl" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xed7451d7, "drm_gem_fb_vmap" },
	{ 0x1586344, "drm_dev_unplug" },
	{ 0x55a0dcf6, "drm_connector_init" },
	{ 0x546e44b0, "drm_add_edid_modes" },
	{ 0xacbc1d8c, "noop_llseek" },
	{ 0x222a2962, "drm_read" },
	{ 0x9a0c92ec, "drm_gem_fb_create_with_dirty" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x4f90058, "drm_gem_fb_vunmap" },
	{ 0xf74bb274, "mod_delayed_work_on" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xbd718a36, "usb_deregister" },
	{ 0x4a35d30d, "drm_mode_set_name" },
	{ 0x8b968446, "usb_string" },
	{ 0xfc81ee3c, "drm_gem_shmem_prime_import_sg_table" },
	{ 0xfb578fc5, "memset" },
	{ 0xd83c4bc6, "_dev_warn" },
	{ 0x2809d200, "drm_dev_put" },
	{ 0x4f0d2899, "drm_connector_update_edid_property" },
	{ 0xead3988b, "param_ops_charp" },
	{ 0x53d3a2be, "drm_connector_attach_edid_property" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x80d9baad, "drm_fbdev_generic_setup" },
	{ 0x19ec1ff1, "drm_atomic_helper_connector_duplicate_state" },
	{ 0x1c6f9597, "drm_simple_display_pipe_init" },
	{ 0xe2d5255a, "strcmp" },
	{ 0xa6e61d1d, "drm_connector_cleanup" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xdd64e639, "strscpy" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0xa38c4c94, "drm_memcpy_from_wc" },
	{ 0x999e8297, "vfree" },
	{ 0x85df9b6c, "strsep" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x9fa7184a, "cancel_delayed_work_sync" },
	{ 0xbc108f6d, "drm_gem_fb_begin_cpu_access" },
	{ 0xc9cfd776, "drm_kms_helper_hotplug_event" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x1cb690aa, "param_ops_bool" },
	{ 0x66cca4f9, "__x86_indirect_thunk_rcx" },
	{ 0x2cf56265, "__dynamic_pr_debug" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0x814f1f43, "drm_helper_probe_single_connector_modes" },
	{ 0xffeedf6a, "delayed_work_timer_fn" },
	{ 0xa6f48c6e, "drm_atomic_helper_connector_reset" },
	{ 0x877e39e8, "drm_mode_config_reset" },
	{ 0x4756e26b, "kmalloc_trace" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xd5049340, "param_ops_int" },
	{ 0x252dd22f, "drm_dev_alloc" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x33b8cd2b, "drm_compat_ioctl" },
	{ 0x53e5916c, "drm_dev_register" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x898cbdaa, "kmalloc_caches" },
	{ 0x41919e7c, "drm_release" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xaa8994e1, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("usb:v303Ap2986d*dc*dsc*dp*ic*isc*ip*in00*");

MODULE_INFO(srcversion, "9C3211A93BEF9607ACB8BA2");
