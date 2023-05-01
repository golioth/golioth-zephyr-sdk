#ifndef __DFU_H__
#define __DFU_H__

struct golioth_client;

#ifdef CONFIG_BOOTLOADER_MCUBOOT

void dfu_on_connect(struct golioth_client *client);
void dfu_main(void);

#else /* CONFIG_BOOTLOADER_MCUBOOT */

static inline void dfu_on_connect(struct golioth_client *client) {}
static inline void dfu_main(void) {}

#endif /* CONFIG_BOOTLOADER_MCUBOOT */

#endif /* __DFU_H__ */
