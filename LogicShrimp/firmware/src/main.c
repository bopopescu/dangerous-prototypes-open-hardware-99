//Due to licensing issues, we can't redistribute the Microchip USB source. 
//You can get it from the Microchip website for free: 
//http://www.microchip.com/usb
//
//1.Download and install the USB source. These files install to c:\Microchip Soultions\ by default.
//2.Place the project folder in the Microchip source install directory (c:\Microchip Soultions\ by default)
//3.Copy usb_config.h & usb_descriptors.c from \Microchip Solutions\USB Device - CDC - Basic Demo\CDC - Basic Demo - Firmware3\ to the project folder.
//4. That's it. You've got the latest source and we're compliant with the license.
//
//Depending on the install location you may need to tweak the include paths under Project->build options.
#include "globals.h"
#include "config.h"

//commandset
//http://www.sump.org/projects/analyzer/protocol/
#define SUMP_RESET 	0x00
#define SUMP_RUN	0x01
#define SUMP_ID		0x02
#define SUMP_DESC	0x04

#define SUMP_XON	0x11
#define SUMP_XOFF 	0x13
#define SUMP_DIV 	0x80
#define SUMP_CNT	0x81
#define SUMP_FLAGS	0x82
#define SUMP_TRIG	0xc0
#define SUMP_TRIG_VALS 0xc1

#define LA_SAMPLE_SIZE  20

//this struct buffers the USB input because the stack doesn't like 1 byte reads
#pragma udata

static struct _usbbuffer{
	unsigned char inbuf[64];
	unsigned char cnt;
	unsigned char rdptr;
} ubuf;

static void init(void);
void usbbufservice(void);
void usbbufflush(void);
unsigned char usbbufgetbyte(unsigned char* c);
unsigned char waitforbyte(void);
unsigned char checkforbyte(void);
void sendok(void);
void send(unsigned char c);
void usbservice(void);

u8 UsbFifoBufferArray[66];
static unsigned long samples, divider;
u8 command[5], clock;
u8 inByte;

#if 1

#pragma code
void main(void)
{
u8 i;


init();			//setup the crystal, pins
hal_sram_parallelInit(); //setup the SRAM, error if one doesn't reply
usbbufflush();	//setup the USB byte buffer

USBDeviceInit();//setup usb

while(1)
	{

	USBDeviceTasks();

	if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1)) continue;
	usbbufservice();//load any USB data into byte buffer
	inByte=waitforbyte();
	switch(inByte)
		{//switch on the command
		case SUMP_RESET://reset
			break;
		case SUMP_ID://SLA0 or 1 backwards: 1ALS
			if( mUSBUSARTIsTxTrfReady() )
				{
				//putsUSBUSART("1ALS"); //doesn't work b/c of 0x00
				UsbFifoBufferArray[0]='1';
				UsbFifoBufferArray[1]='A';
				UsbFifoBufferArray[2]='L';
				UsbFifoBufferArray[3]='S';
				putUnsignedCharArrayUsbUsart(UsbFifoBufferArray,4);
				}
			break;
		case SUMP_RUN://arm the triger

//
//
//	CAPTURE SETUP
//
//
			//ARMED, turn on LED
			hal_logicshrimp_setLed(PORT_ON);

			//setup the SRAM for record
			hal_sram_parallelInit();
			hal_sram_setup_capture();//setup read, turn PIC->SRAM pins input

			//enable the buffer
			hal_logicshrimp_BufferEnable();

			//ADDRESS TRACKING//
			//Need to know where in the SRAM we are//
			//TODO:setup a external counter on C7 to track the SRAM address (/8 prescaler)
			//TODO:interrupt on samples number of ticks

			//start the clock source
			//this is the 20MHz external clock
			//OR PWM from PIC pin RC7/RP18
			#define EXTERNAL 0
			clock=EXTERNAL;
			if(clock==EXTERNAL){
				TRISCbits.TRISC7=1;//disable PIC clock pin
				hal_logicshrimp_ClockGateEnable();
			}else{
				//TODO:setup the PWM for the requested frequency
				//we fake it for now....
				i=250;
				while(i--){
					SCLK=PORT_ON;
					Nop();
					SCLK=PORT_OFF;
				};
			}

			//this should come from the long command below
			samples=128;

//
//
//	TRIGGER (ignore for now)
//
//
			//TODO: sample and trigger tracking section
			//TODO: wait for change interrupt (if trigger used)
			
//
//
//	CAPTURE
//
//

			//TODO: count the desired number of samples with a timer
				i=250;
				while(i--){
					Nop();
				};

			//raise CS to end write to SRAM
			hal_sram_end_capture(); 

//
//
//	CLEANUP
//
//

			//stop the sample clock
			hal_logicshrimp_ClockGateDisable();
			
			hal_logicshrimp_BufferDisable();
	
			//restore uC clock to output
			LATCbits.LATC7=0; //clock start low
			TRISCbits.TRISC7=0; //uC clock out to output

			hal_logicshrimp_setLed(PORT_OFF);

//
//
//			DUMP
//
//
//			//read samples and dump them out USB
			//write the SRAM command to read from the beginning
			hal_sram_setup_dump();

			//loop untill all is read
			while(1)
				{
				usbservice();
				if(USBUSARTIsTxTrfReady())
					{

					static u8 USBWriteCount;
					USBWriteCount=0;

					for(i=0; i<64; i++)
						{
						SCLK=PORT_ON;
						Nop();
						UsbFifoBufferArray[USBWriteCount]= PORTA & 0b1111; //get the lower 4 bits of PORTA
						SCLK=PORT_OFF;
	
						USBWriteCount++;

						samples--;
						if(samples==0) break;//send 64/128/512/1024 samples exactly! break at the end of samples
						}
					putUnsignedCharArrayUsbUsart(UsbFifoBufferArray,USBWriteCount);
					}
					if(samples==0)break; //send 64/128/512/1024 samples exactly! break at the end of samples
				}
			break;
		case SUMP_XON://resume send data
			//	xflow=1;
			break;
		case SUMP_XOFF://pause send data
			//	xflow=0;
			break;
		default://long command
			command[0]=inByte;//store first command byte
			command[1]=waitforbyte();
			command[2]=waitforbyte();
			command[3]=waitforbyte();
			command[4]=waitforbyte();
			switch(command[0]){
		
				case SUMP_TRIG: //set CN on these pins
				//if(sumpRX.command[1] & 0b10000)	CNEN2|=0b1; //AUX
				//if(sumpRX.command[1] & 0b1000)  CNEN2|=0b100000;
				//if(sumpRX.command[1] & 0b100)   CNEN2|=0b1000000;
				//if(sumpRX.command[1] & 0b10)  	CNEN2|=0b10000000;
				//if(sumpRX.command[1] & 0b1) 	CNEN2|=0b100000000;
				break;
				case SUMP_FLAGS:
				/*
				sumpPadBytes=0;//if user forgot to uncheck chan groups 2,3,4, we can send padding bytes
				if(sumpRX.command[1] & 0b100000) sumpPadBytes++;
				if(sumpRX.command[1] & 0b10000) sumpPadBytes++;
				if(sumpRX.command[1] & 0b1000) sumpPadBytes++;
				break;
				*/
				case SUMP_CNT:
				samples=command[2];
				samples<<=8;
				samples|=command[1];
				samples=(samples+1)*4;
				//constrain to 256K
				if(samples>LA_SAMPLE_SIZE) samples=LA_SAMPLE_SIZE;
				break;
				case SUMP_DIV:
				divider=command[3];
				divider<<=8;
				divider|=command[2];
				divider<<=8;
				divider|=command[1];
		
				//setup clock source -
				//20mhz direct from OSC
				//0-Xmhz with PIC PWM
		
				//convert from SUMP 100MHz clock to our 12MIPs
				//l=((l+1)*16)/100;
				//l=((l+1)*4)/25;
		
				//adjust downwards a bit
				//if(l>0x10)
				//	l-=0x10;
				//else //fast as possible
				//	l=1;
		
				//setup PR register
				//PR5=(l>>16);//most significant word
				//PR4=l;//least significant word
		
				break;
			}
		}
	}
CDCTxService();
}

//}//end main

void send(unsigned char c){
	unsigned char b[2];

  	if( mUSBUSARTIsTxTrfReady() ){ //it's always ready, but this could be done better
		b[0]=c;
		putUnsignedCharArrayUsbUsart(b,1);
	}	

}

void sendok(void){
	unsigned char b[2];

  	if( mUSBUSARTIsTxTrfReady() ){ //it's always ready, but this could be done better
		b[0]='1';//answer OK
		putUnsignedCharArrayUsbUsart(b,1);
	}	

}
void usbservice(){
	USBDeviceTasks(); 
	CDCTxService();
}
unsigned char waitforbyte(void){
	unsigned char inbuf;

	if(usbbufgetbyte(&inbuf)) return inbuf; //return data
	//wait for more USB data
	//services USB peripheral
	while(1){
		USBDeviceTasks(); 
	   	if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1)){
			return 0; //goto USB_loop_jump; //USB gone, fail out to main loop
		}
		usbbufservice();//load any USB data into byte buffer
		if(usbbufgetbyte(&inbuf)) return inbuf; //wait for more data
	    CDCTxService();
	}

} 

unsigned char checkforbyte(void){
	unsigned char inbuf;

	if(usbbufgetbyte(&inbuf)) return 1; //return data
	//wait for more USB data
	//services USB peripheral
	USBDeviceTasks(); 
   	if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1)){
		return 1; //goto USB_loop_jump; //USB gone, fail out to main loop
	}
	usbbufservice();//load any USB data into byte buffer
	if(usbbufgetbyte(&inbuf)) return 1; //wait for more data
    CDCTxService();
	return 0;
} 



static void init(void){
	unsigned int cnt = 2048;
	
	//all pins digital
    ANCON0 = 0xFF;                  
    ANCON1 = 0b00011111;// updated for lower power consumption. See datasheet page 343                  

	//make sure everything is input (should be on startup, but just in case)
	TRISA=0xff;
	TRISB=0xff;
	TRISC=0b11111111; 

	hal_logicshrimp_pinsetup();

	//on 18f24j50 we must manually enable PLL and wait at least 2ms for a lock
	OSCTUNEbits.PLLEN = 1;  //enable PLL
	while(cnt--); //wait for lock

}

void usbbufservice(void){
	if(ubuf.cnt==0){//if the buffer is empty, get more data
		ubuf.cnt = getUnsignedCharArrayUsbUart(ubuf.inbuf,64);
		ubuf.rdptr=0;
	}
}

//puts a byte from the buffer in the byte, returns 1 if byte
unsigned char usbbufgetbyte(unsigned char* c){
	if(ubuf.cnt>0){
		*c=ubuf.inbuf[ubuf.rdptr];
		ubuf.cnt--;
		ubuf.rdptr++;
		return 1;
	}
	return 0;
}

void usbbufflush(void){
	ubuf.cnt = 0;
	ubuf.rdptr=0;
}


#endif

//
//
//the stack calls these, if they aren't here we get errors. 
//
//
void USBCBSuspend(void){}
void USBCBWakeFromSuspend(void){}
void USBCB_SOF_Handler(void){}
void USBCBErrorHandler(void){}
void USBCBCheckOtherReq(void){USBCheckCDCRequest();}//end
void USBCBStdSetDscHandler(void){}//end
void USBCBInitEP(void){CDCInitEP();}
BOOL USER_USB_CALLBACK_EVENT_HANDLER(USB_EVENT event, void *pdata, WORD size){
    switch(event){
        case EVENT_CONFIGURED: 
            USBCBInitEP();
            break;
        case EVENT_SET_DESCRIPTOR:
            USBCBStdSetDscHandler();
            break;
        case EVENT_EP0_REQUEST:
            USBCBCheckOtherReq();
            break;
        case EVENT_SOF:
            USBCB_SOF_Handler();
            break;
        case EVENT_SUSPEND:
            USBCBSuspend();
            break;
        case EVENT_RESUME:
            USBCBWakeFromSuspend();
            break;
        case EVENT_BUS_ERROR:
            USBCBErrorHandler();
            break;
        case EVENT_TRANSFER:
            Nop();
            break;
        default:
            break;
    }      
    return TRUE; 
}

#if 0

#define REMAPPED_RESET_VECTOR_ADDRESS			0x800
#define REMAPPED_HIGH_INTERRUPT_VECTOR_ADDRESS	0x808
#define REMAPPED_LOW_INTERRUPT_VECTOR_ADDRESS	0x818

//We didn't use the low priority interrupts, 
// but you could add your own code here
#pragma interruptlow InterruptHandlerLow
void InterruptHandlerLow(void){}
/*
//We didn't use the low priority interrupts, 
// but you could add your own code here
#pragma interrupthigh InterruptHandlerHigh
void InterruptHandlerHigh(void){}
*/
//these statements remap the vector to our function
//When the interrupt fires the PIC checks here for directions
#pragma code REMAPPED_HIGH_INTERRUPT_VECTOR = REMAPPED_HIGH_INTERRUPT_VECTOR_ADDRESS
void Remapped_High_ISR (void){
     _asm goto POVInterrupt _endasm
}

#pragma code REMAPPED_LOW_INTERRUPT_VECTOR = REMAPPED_LOW_INTERRUPT_VECTOR_ADDRESS
void Remapped_Low_ISR (void){
     _asm goto InterruptHandlerLow _endasm
}

//relocate the reset vector
extern void _startup (void);  
#pragma code REMAPPED_RESET_VECTOR = REMAPPED_RESET_VECTOR_ADDRESS
void _reset (void){
    _asm goto _startup _endasm
}
//set the initial vectors so this works without the bootloader too.
#pragma code HIGH_INTERRUPT_VECTOR = 0x08
void High_ISR (void){
     _asm goto REMAPPED_HIGH_INTERRUPT_VECTOR_ADDRESS _endasm
}
#pragma code LOW_INTERRUPT_VECTOR = 0x18
void Low_ISR (void){
     _asm goto REMAPPED_LOW_INTERRUPT_VECTOR_ADDRESS _endasm
}


#endif