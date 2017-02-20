/*
 * https://forum.pjrc.com/threads/186-Teensy-3-fault-handler-demonstration?highlight=crash+handler
 *
 * call this from setup():
 *     enableFaultHandler();
 *
 * On crash you see something like:
 *   !!!! Crashed at pc=0x490, lr=0x55B.
 *
 * which you can interpret thusly:
 *   $ arm-none-eabi-addr2line -s -f -C -e main.elf 0x490 0x55B
 *     Print:: println(char const*)
 *     Print.h:47
 *     main
 *     main_crash.cpp:86
 */

#define SCB_SHCSR_USGFAULTENA (uint32_t)1<<18
#define SCB_SHCSR_BUSFAULTENA (uint32_t)1<<17
#define SCB_SHCSR_MEMFAULTENA (uint32_t)1<<16

#define SCB_SHPR1_USGFAULTPRI *(volatile uint8_t *)0xE000ED20
#define SCB_SHPR1_BUSFAULTPRI *(volatile uint8_t *)0xE000ED19
#define SCB_SHPR1_MEMFAULTPRI *(volatile uint8_t *)0xE000ED18

// enable bus, usage, and mem fault handlers.
void enableFaultHandler()
{
  SCB_SHCSR |= SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA | SCB_SHCSR_MEMFAULTENA;
}


extern "C" {
void __attribute__((naked)) _fault_isr () {
  uint32_t* sp=0;
  // this is from "Definitive Guide to the Cortex M3" pg 423
  asm volatile ( "TST LR, #0x4\n\t"   // Test EXC_RETURN number in LR bit 2
		 "ITE EQ\n\t"         // if zero (equal) then
		 "MRSEQ %0, MSP\n\t"  //   Main Stack was used, put MSP in sp
		 "MRSNE %0, PSP\n\t"  // else Process stack was used, put PSP in sp
		 : "=r" (sp) : : "cc");

  Serial.print("!!!! Crashed at pc=0x");
  Serial.print(sp[6], 16);
  Serial.print(", lr=0x");
  Serial.print(sp[5], 16);
  Serial.println(".");
  
  Serial.flush();
  
  // allow USB interrupts to preempt us:
  SCB_SHPR1_BUSFAULTPRI = (uint8_t)255;
  SCB_SHPR1_USGFAULTPRI = (uint8_t)255;
  SCB_SHPR1_MEMFAULTPRI = (uint8_t)255;

  while (1) {
      digitalWrite(13, HIGH);
      delay(100);
      digitalWrite(13, LOW);
      delay(100);

    asm volatile (
		  "WFI" // Wait For Interrupt.
		  );
  }
}
}

void hard_fault_isr(void)       __attribute__ ((alias("_fault_isr")));
void memmanage_fault_isr(void)  __attribute__ ((alias("_fault_isr")));
void bus_fault_isr(void)        __attribute__ ((alias("_fault_isr")));
void usage_fault_isr(void)      __attribute__ ((alias("_fault_isr")));
