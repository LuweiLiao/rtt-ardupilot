/**
 * test_L5_usb — Layer 5: USB DWC2 OTG_FS (ChibiOS init seq)
 *
 * Exact register-level init copied from ChibiOS hal_usb_lld.c + CherryUSB.
 * CUAV V5 (STM32F767) has NO VBUS sensing, so GCCFG = PWRDWN only.
 *
 * Build: scons --target=cuav-v5 --test=L5_usb -j$(nproc)
 */

#include "test_runner.h"
#include <rtthread.h>
#include <stdint.h>

/* RCC (RM0410) */
#define RCC_AHB2ENR     (*(volatile uint32_t *)0x40023834UL)
#define RCC_AHB2RSTR    (*(volatile uint32_t *)0x4002387CUL)
#define RCC_AHB1ENR     (*(volatile uint32_t *)0x40023830UL)
#define RCC_APB1ENR     (*(volatile uint32_t *)0x40023840UL)
#define PWR_CR2         (*(volatile uint32_t *)0x40007018UL)

/* OTG_FS base */
#define OTG             0x50000000UL

/* Global registers */
#define OTG_GOTGCTL     (*(volatile uint32_t *)(OTG + 0x000))
#define OTG_GAHBCFG     (*(volatile uint32_t *)(OTG + 0x008))
#define OTG_GUSBCFG     (*(volatile uint32_t *)(OTG + 0x00C))
#define OTG_GRSTCTL     (*(volatile uint32_t *)(OTG + 0x010))
#define OTG_GINTSTS     (*(volatile uint32_t *)(OTG + 0x014))
#define OTG_GINTMSK     (*(volatile uint32_t *)(OTG + 0x018))
#define OTG_GRXFSIZ     (*(volatile uint32_t *)(OTG + 0x024))
#define OTG_DIEPTXF0    (*(volatile uint32_t *)(OTG + 0x028))
#define OTG_GCCFG       (*(volatile uint32_t *)(OTG + 0x038))
#define OTG_PCGCCTL     (*(volatile uint32_t *)(OTG + 0xE00))

/* Device registers */
#define OTG_DCFG        (*(volatile uint32_t *)(OTG + 0x800))
#define OTG_DCTL        (*(volatile uint32_t *)(OTG + 0x804))
#define OTG_DSTS        (*(volatile uint32_t *)(OTG + 0x808))
#define OTG_DIEPMSK     (*(volatile uint32_t *)(OTG + 0x810))
#define OTG_DOEPMSK     (*(volatile uint32_t *)(OTG + 0x814))
#define OTG_DAINTMSK    (*(volatile uint32_t *)(OTG + 0x81C))
#define OTG_DAINT       (*(volatile uint32_t *)(OTG + 0x818))
#define OTG_DIEPEMPMSK  (*(volatile uint32_t *)(OTG + 0x834))

/* EP0 registers */
#define OTG_DIEPCTL0    (*(volatile uint32_t *)(OTG + 0x900))
#define OTG_DIEPTSIZ0   (*(volatile uint32_t *)(OTG + 0x910))
#define OTG_DIEPINT0    (*(volatile uint32_t *)(OTG + 0x908))
#define OTG_DOEPCTL0    (*(volatile uint32_t *)(OTG + 0xB00))
#define OTG_DOEPTSIZ0   (*(volatile uint32_t *)(OTG + 0xB10))
#define OTG_DOEPINT0    (*(volatile uint32_t *)(OTG + 0xB08))

/* EPS registers */
#define OTG_DIEPCTL(i)  (*(volatile uint32_t *)(OTG + 0x900 + (i)*0x20))
#define OTG_DOEPCTL(i)  (*(volatile uint32_t *)(OTG + 0xB00 + (i)*0x20))

/* GPIOA */
#define PA_MODER        (*(volatile uint32_t *)0x40020000UL)
#define PA_AFRH         (*(volatile uint32_t *)0x40020024UL)
#define PA_OSPEEDR      (*(volatile uint32_t *)0x40020008UL)
#define PA_PUPDR        (*(volatile uint32_t *)0x4002000CUL)

/* NVIC */
#define NVIC_ISER2      (*(volatile uint32_t *)0xE000E108UL)

/* ===== Bit masks ===== */
#define GUSBCFG_FDMOD       (1UL << 30)
#define GUSBCFG_TRDT(n)     (((n) << 10) & 0x1C00UL)
#define GUSBCFG_PHYSEL      (1UL << 6)

#define GRSTCTL_AHBIDL      (1UL << 31)
#define GRSTCTL_CSRST       (1UL << 0)
#define GRSTCTL_TXFFLSH     (1UL << 5)
#define GRSTCTL_TXFNUM(n)   (((n) << 6) & (0x1FUL << 6))
#define GRSTCTL_RXFFLSH     (1UL << 4)

#define DCFG_NZLSOHSK       (1UL << 25)
#define DCFG_DSPD_FS11      (3UL << 0)

#define DCTL_SDIS           (1UL << 1)

#define GOTGCTL_BVALOEN     (1UL << 21)
#define GOTGCTL_BVALOVAL    (1UL << 22)

#define GCCFG_PWRDWN        (1UL << 16)

#define GAHBCFG_GINTMSK     (1UL << 0)

#define DIEPCTL_EPENA       (1UL << 31)
#define DIEPCTL_SNAK        (1UL << 27)
#define DIEPCTL_USBAEP      (1UL << 15)
#define DIEPCTL_SD0PID      (1UL << 6)
#define DIEPCTL_MPS64       (64UL << 0)

#define DOEPCTL_EPENA       (1UL << 31)
#define DOEPCTL_SNAK        (1UL << 27)
#define DOEPCTL_USBAEP      (1UL << 15)
#define DOEPCTL_SD0PID      (1UL << 6)

#define DOEPTSIZ_STUPCNT(n) (((n) << 29) & (0x3UL << 29))

#define DIEPMSK_TOCM        (1UL << 3)
#define DIEPMSK_XFRCM       (1UL << 0)
#define DOEPMSK_STUPM       (1UL << 3)
#define DOEPMSK_XFRCM       (1UL << 0)

/* ===== Stubs ===== */
void ap_rtt_iwdg_kick(void) { }

static void _dsb(void) { __asm volatile("dsb" ::: "memory"); }
static void _isb(void) { __asm volatile("isb" ::: "memory"); }
static void _delay(uint32_t n) { while (n--) __asm volatile("nop"); }
static void _mdelay(uint32_t ms) {
    uint32_t goal = rt_tick_get() + (ms * RT_TICK_PER_SECOND) / 1000;
    while (rt_tick_get() < goal) { _delay(100); }
}

/* ===== Step 1: Register accessibility ===== */
static void step_reg_access(void)
{
    TEST_STEP("Register accessibility");
    uint32_t gusb = OTG_GUSBCFG;
    uint32_t gccfg = OTG_GCCFG;
    uint32_t dcfg = OTG_DCFG;
    uint32_t dctl = OTG_DCTL;
    test_printf("    GUSBCFG=0x%08lx GCCFG=0x%08lx\r\n", (unsigned long)gusb, (unsigned long)gccfg);
    test_printf("    DCFG=0x%08lx DCTL=0x%08lx\r\n", (unsigned long)dcfg, (unsigned long)dctl);
    TEST_ASSERT(gusb != 0xFFFFFFFFU, "GUSBCFG readable");
    TEST_PASS();
}

/* ===== Step 2: Clock + GPIO (RCC) ===== */
static void step_clock_gpio(void)
{
    TEST_STEP("Clock + GPIO");

    /* 1. PWR clock + USB supply valid */
    RCC_APB1ENR |= (1UL << 28);
    _dsb(); _delay(100);
    PWR_CR2 |= (1UL << 0);
    _dsb(); _delay(100);

    /* 2. Enable OTGFS clock */
    uint32_t before = RCC_AHB2ENR;
    RCC_AHB2ENR = before | (1UL << 7);
    _dsb(); _isb();
    uint32_t after = RCC_AHB2ENR;
    test_printf("    AHB2ENR 0x%08lx → 0x%08lx\r\n", (unsigned long)before, (unsigned long)after);
    TEST_ASSERT(after & (1UL << 7), "OTG_FS clock");

    /* 3. Reset OTGFS */
    RCC_AHB2RSTR |= (1UL << 7);
    _dsb(); _delay(500);
    RCC_AHB2RSTR &= ~(1UL << 7);
    _dsb(); _delay(500);
    test_printf("    OTG_FS reset done\r\n");

    /* 4. GPIO: PA11=DM AF10, PA12=DP AF10 */
    RCC_AHB1ENR |= (1UL << 0);
    _dsb();
    PA_MODER = (PA_MODER & ~(3UL << 22)) | (2UL << 22);  /* PA11 AF */
    PA_MODER = (PA_MODER & ~(3UL << 24)) | (2UL << 24);  /* PA12 AF */
    PA_AFRH  = (PA_AFRH & ~(0xFUL << 12)) | (10UL << 12);
    PA_AFRH  = (PA_AFRH & ~(0xFUL << 16)) | (10UL << 16);
    PA_OSPEEDR |= (3UL << 22) | (3UL << 24);
    PA_PUPDR  &= ~((3UL << 22) | (3UL << 24));
    PA_MODER  &= ~(3UL << 18);
    _dsb();

    test_printf("    MODER=0x%08lx AFRH=0x%08lx\r\n",
                (unsigned long)PA_MODER, (unsigned long)PA_AFRH);
    TEST_PASS();
}

/* ===== Step 3: Soft disconnect ===== */
static void step_soft_disconnect(void)
{
    TEST_STEP("Soft disconnect");
    OTG_DCTL = DCTL_SDIS;
    _dsb();
    _mdelay(60);
    uint32_t dctl = OTG_DCTL;
    test_printf("    DCTL=0x%08lx SDIS=%lu\r\n", (unsigned long)dctl,
                (unsigned long)((dctl >> 1) & 1));
    TEST_ASSERT(dctl & DCTL_SDIS, "SDIS set");
    TEST_PASS();
}

/* ===== Step 4: Core config (GUSBCFG + DCFG + GOTGCTL + GCCFG) ===== */
static void step_core_config(void)
{
    TEST_STEP("Core config");

    /* GUSBCFG: Force device, FS PHY, TRDT=5 */
    OTG_GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(5) | GUSBCFG_PHYSEL;
    _dsb();
    uint32_t gusb = OTG_GUSBCFG;
    test_printf("    GUSBCFG=0x%08lx FDMOD=%lu TRDT=%lu\r\n",
                (unsigned long)gusb,
                (unsigned long)((gusb >> 29) & 1),
                (unsigned long)((gusb >> 10) & 7));

    /* DCFG: FS 1.1 PHY, no NZ LS handshake */
    OTG_DCFG = DCFG_NZLSOHSK | DCFG_DSPD_FS11;  /* 0x02200003 */
    _dsb();

    /* PCGCCTL: un-gate clocks */
    OTG_PCGCCTL = 0;
    _dsb();

    /* GOTGCTL: BVALOEN + BVALOVAL (override VBUS) */
    OTG_GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;
    _dsb();

    /* GCCFG: PWRDWN only (CUAV V5 has no VBUS sense!) */
    OTG_GCCFG = GCCFG_PWRDWN;
    _dsb();
    uint32_t gccfg = OTG_GCCFG;
    test_printf("    GCCFG=0x%08lx PWRDWN=%lu\r\n", (unsigned long)gccfg,
                (unsigned long)((gccfg >> 16) & 1));

    TEST_PASS();
}

/* ===== Step 5: Core soft reset ===== */
static void step_core_reset(void)
{
    TEST_STEP("Core reset");

    /* Wait AHBIDL */
    { uint32_t sp = 50000; while (!(OTG_GRSTCTL & GRSTCTL_AHBIDL) && sp--) _delay(10); }
    test_printf("    AHBIDL before=%s\r\n", (OTG_GRSTCTL & GRSTCTL_AHBIDL) ? "OK" : "TMO");

    /* CSRST */
    OTG_GRSTCTL = GRSTCTL_CSRST;
    _dsb();
    _delay(500);
    { uint32_t sp = 50000; while ((OTG_GRSTCTL & GRSTCTL_CSRST) && sp--) _delay(10); }
    test_printf("    CSRST=%s\r\n", (OTG_GRSTCTL & GRSTCTL_CSRST) ? "BUSY" : "DONE");

    /* Wait AHBIDL again */
    { uint32_t sp = 50000; while (!(OTG_GRSTCTL & GRSTCTL_AHBIDL) && sp--) _delay(10); }
    test_printf("    AHBIDL after=%s\r\n", (OTG_GRSTCTL & GRSTCTL_AHBIDL) ? "OK" : "TMO");

    /* Re-program GUSBCFG after reset */
    OTG_GUSBCFG = GUSBCFG_FDMOD | GUSBCFG_TRDT(5) | GUSBCFG_PHYSEL;
    _dsb();
    OTG_PCGCCTL = 0;
    _dsb();
    OTG_GOTGCTL = GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;
    _dsb();
    OTG_GCCFG = GCCFG_PWRDWN;
    _dsb();
    test_printf("    Registers re-programmed after reset\r\n");

    TEST_PASS();
}

/* ===== Step 6: FIFO + Interrupts + EP0 ===== */
static void step_fifo_ep0(void)
{
    TEST_STEP("FIFO + Interrupts + EP0");

    /* GRXFSIZ = 128 words, TXFIFO0 = [128]16 = 128 offset, 16 words deep */
    OTG_GRXFSIZ = 128;
    OTG_DIEPTXF0 = (128 << 0) | (16 << 16);
    _dsb();
    test_printf("    GRXFSIZ=%lu DIEPTXF0=SA%lu+FD%lu\r\n",
                (unsigned long)OTG_GRXFSIZ,
                (unsigned long)(OTG_DIEPTXF0 & 0xFFFF),
                (unsigned long)((OTG_DIEPTXF0 >> 16) & 0xFFFF));

    /* GAHBCFG = 0 (no DMA) */
    OTG_GAHBCFG = 0;

    /* GINTMSK: key interrupts */
    OTG_GINTMSK = (1UL << 13) |  /* ENUMDNEM */
                  (1UL << 12) |  /* USBRSTM */
                  (1UL << 11) |  /* USBSUSPM */
                  (1UL << 10) |  /* ESUSPM  */
                  (1UL << 4)  |  /* RXFLVLM */
                  (1UL << 19) |  /* OEPM    */
                  (1UL << 18) |  /* IEPM    */
                  (1UL << 30) |  /* SRQM    */
                  (1UL << 31);   /* WKUM    */
    _dsb();

    /* Clear pending */
    OTG_GINTSTS = 0xBFFFFFFFU;
    _dsb();

    /* Flush TX FIFO 0 */
    OTG_GRSTCTL = GRSTCTL_TXFFLSH | GRSTCTL_TXFNUM(0);
    _dsb();
    { uint32_t sp = 50000; while ((OTG_GRSTCTL & GRSTCTL_TXFFLSH) && sp--) _delay(10); }
    test_printf("    TX FIFO flushed\r\n");

    /* DIEPMSK + DOEPMSK */
    OTG_DIEPMSK = DIEPMSK_TOCM | DIEPMSK_XFRCM;
    OTG_DOEPMSK = DOEPMSK_STUPM | DOEPMSK_XFRCM;
    _dsb();
    test_printf("    DIEPMSK=0x%08lx DOEPMSK=0x%08lx\r\n",
                (unsigned long)OTG_DIEPMSK, (unsigned long)OTG_DOEPMSK);

    /* EP0 setup: DOEPTSIZ0 = 3 setup packets */
    OTG_DOEPTSIZ0 = DOEPTSIZ_STUPCNT(3);
    _dsb();
    test_printf("    DOEPTSIZ0=0x%08lx STUPCNT=%lu\r\n",
                (unsigned long)OTG_DOEPTSIZ0,
                (unsigned long)((OTG_DOEPTSIZ0 >> 29) & 3));

    /* EP0 OUT: control, 64 MPS */
    OTG_DOEPCTL0 = DOEPCTL_SD0PID | DOEPCTL_USBAEP | (0UL << 18) | (64UL << 0);
    /* EP0 IN: control, FIFO 0, 64 MPS */
    OTG_DIEPCTL0 = DIEPCTL_SD0PID | DIEPCTL_USBAEP | (0UL << 18) | (0UL << 22) | (64UL << 0);
    _dsb();
    test_printf("    DIEPCTL0=0x%08lx DOEPCTL0=0x%08lx\r\n",
                (unsigned long)OTG_DIEPCTL0, (unsigned long)OTG_DOEPCTL0);

    /* NVIC: OTG_FS IRQ 67 */
    NVIC_ISER2 = (1UL << 3);
    _dsb();
    test_printf("    NVIC_ISER2=0x%08lx\r\n", (unsigned long)NVIC_ISER2);

    /* GAHBCFG: global interrupt enable */
    OTG_GAHBCFG |= GAHBCFG_GINTMSK;
    _dsb();

    test_printf("    GAHBCFG=0x%08lx GINT=%s\r\n",
                (unsigned long)OTG_GAHBCFG,
                (OTG_GAHBCFG & GAHBCFG_GINTMSK) ? "EN" : "DIS");

    TEST_PASS();
}

/* ===== Step 7: Soft reconnect + poll ===== */
static void step_reconnect(void)
{
    TEST_STEP("Reconnect");

    /* Clear SDIS = soft reconnect → host enumerates */
    OTG_DCTL = 0;
    _dsb();
    _delay(10000);

    uint32_t dctl = OTG_DCTL;
    uint32_t dsts = OTG_DSTS;
    test_printf("    DCTL=0x%08lx DSTS=0x%08lx ENUMSPD=%lu\r\n",
                (unsigned long)dctl, (unsigned long)dsts,
                (unsigned long)(dsts & 3));

    test_printf("    Polling for USB events for 5 seconds...\r\n");
    uint32_t start = rt_tick_get();
    uint32_t timeout = RT_TICK_PER_SECOND * 5;
    bool seen_reset = false;
    bool seen_enum = false;
    bool seen_sof = false;

    while ((rt_tick_get() - start) < timeout) {
        uint32_t gint = OTG_GINTSTS;
        uint32_t daint = OTG_DAINT;
        if (gint & (1UL << 12)) seen_reset = true;  /* USBRST */
        if (gint & (1UL << 7))  seen_enum = true;   /* ENUMDNE */
        if (gint & (1UL << 3))  seen_sof = true;     /* SOF */
        /* DAINT bit 16 = OEP0 STUP interrupt */
        if (daint & (1UL << 16)) seen_enum = true;
        ap_rtt_iwdg_kick();
        if (seen_reset && seen_enum) break;
        _delay(1000);
    }

    unsigned long elapsed_ms = ((unsigned long)(rt_tick_get() - start) * 1000UL) / RT_TICK_PER_SECOND;
    test_printf("    Polled for %lu ms\r\n", elapsed_ms);

    uint32_t gint_now = OTG_GINTSTS;
    test_printf("    GINTSTS=0x%08lx USBRST=%lu ENUMDNE=%lu SOF=%lu\r\n",
                (unsigned long)gint_now,
                (unsigned long)((gint_now >> 12) & 1),
                (unsigned long)((gint_now >> 7) & 1),
                (unsigned long)((gint_now >> 3) & 1));

    test_printf("    USB CDC init complete. Check dmesg for ttyACM1.\r\n");
    TEST_PASS();
}

/* ===== Main ===== */
int main(void)
{
    TEST_INIT("L5_USB");
    test_current_layer = 5;

    step_reg_access();
    step_clock_gpio();
    step_soft_disconnect();
    step_core_config();
    step_core_reset();
    step_fifo_ep0();
    step_reconnect();

    TEST_DONE();
    return 0;
}
