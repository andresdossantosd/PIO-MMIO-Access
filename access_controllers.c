/*
	****************  Nombre y Apellidos  *************************

	             Andres Dos Santos De Andrade

	****************  Nombre y Apellidos  *************************
*/
#include <sys/io.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>


#define CONFIG_DIR 0xCF8
/* en CONFIG_DIR se especifica dir. a leer/escribir: bus|slot|func|registro con el formato:
|    31    |(30-24) |(23-16)|(15-11)  |(10-8)     |(7-2)      |(1-0)| 
|Enable Bit|Reserved|BusNum |DeviceNum|FunctionNum|RegisterNum| 0 0 |    */

#define CONFIG_DAT 0xCFC
// Si se lee de CONFIG_DAT se obtiene contenido de registro especificado en CONFIG_DIR
// Si se escribe en CONFIG_DAT se modifica contenido de registro especificado en CONFIG_DIR

#define CONFIG_DIR_ENABLE_BIT_MASK 0x80000000
#define CONFIG_DIR_RESERVED_MASK   0x7F000000
#define CONFIG_DIR_BUS_MASK        0x00FF0000  
#define CONFIG_DIR_DEVICE_MASK     0x0000F800
#define CONFIG_DIR_FUNCTION_MASK   0x00000700
#define CONFIG_DIR_REGISTER_MASK   0x000000FC  

// Estructura para almacenar la configuración
typedef struct {
    unsigned int enableBit   : 1;
    unsigned int reserved   : 7;
    unsigned int busNum     : 8;
    unsigned int deviceNum  : 5;
    unsigned int functionNum: 3;
    unsigned int registerNum: 6;
} ConfigDir;

// Función para construir el número de 32 bits
uint32_t construirNumero(ConfigDir *config) {
    return ((config->enableBit << 31) |
            (config->reserved << 24) |
            (config->busNum << 16) |
            (config->deviceNum << 11) |
            (config->functionNum << 8) |
            (config->registerNum << 2));
}

// Struct que se va a utilizar para almacenar el contenido al realizar el mmap
// Consideramos la adicion del parametro relleno ya que no lo consideramos importantes
// El registro que contiene la version, estara ubicado en BAR5 + 0x010 (offset)
typedef volatile struct tagHBA_MEM
{
	// 0x00 - 0x2B, Generic Host Control
	/***      parametros de relleno que no utilizamos      ***/
	uint32_t relleno[4];		
	/***      la version esta a partir de 0x10      ***/
	uint16_t vs_minor;		// 0x10, Version
	uint16_t vs_major;		//
} HBA_MEM;


// Obtener version con MMIO de AHCI
void mmio_access_ahci(unsigned long int bar5){
	// Asumimos el tam=4096 debido a que a dobnde apunta BAR5 no es directamente en el registro que contiene la version
	int fd, tam=4096; 
	// Volatile: asegurarse de que todo acceso a la variable corresponde a un acceso al registro del dispositivo vinculado con la misma.
	// La estructura ya esta declarada como volatile
	HBA_MEM *dev_info; 
	// O_DSYNC: accesos no usan cache
	if ((fd = open("/dev/mem", O_RDONLY|O_DSYNC))<0) { 
		perror("open"); 
		return ; 
	}
	// Obtener direccion logica asociada a direccion fisica del dispositivo
	if ((dev_info = mmap(NULL, tam, PROT_READ, MAP_SHARED, fd, bar5))==MAP_FAILED) {
		perror("mmap"); 
		return ; 
	}
	/*
		**********
		**********
		**********  En el struct tagHBA_MEM, antes teniamos un parametro uint32_t que recibia 00010100, por lo que asumimos que se referia a
		**********  que el Major Version Number era 1, y el Minor Version Number 1, por lo que la version es 1.3.
		**********
		**********  Por esta razon, se dividio en dos uint16_t, para separar el major con minor version
		**********
		**********  Ademas, se anadieron parametros de relleno que no utilizabamos, pero con mmap al ajustar la variable BAR5 con 
		**********  la direccion fisica, al agregarle el offset de 0x10 para acceder la version number directamente, daba erro y acceso erroneo
		**********
	*/

	/*
	    **********
	    **********  31:16 RO 0001h Major Version Number (MJR): Indicates the major version is “1”
	    **********  15:00 RO 0100h Minor Version Number (MNR): Indicates the minor version is “10”
	    **********
	*/
	// Si recibo 0000h en el major y 0905h en el minor, si respeteare el caso de que imprima AHCI 0.95, ya que
	// (dev_info->vs_minor & 0xFF) | ((dev_info->vs_minor & 0xF00) >> 4) lo que hace es que se queda en el minor con el 9 y con el 5.
	printf("           AHCI Versión %x.%x\n", dev_info->vs_major, (dev_info->vs_minor & 0xFF) | ((dev_info->vs_minor & 0xF00) >> 4));
}

ConfigDir* init_check_device(uint8_t bus, uint8_t device){
	ConfigDir *initConfigPtr;
	initConfigPtr = (ConfigDir *)malloc(sizeof(ConfigDir));
	if (initConfigPtr == NULL) { 
		fprintf(stderr, "Error: No se pudo asignar memoria.\n"); 
		return NULL; 
	}
	initConfigPtr->enableBit     =  1;
	initConfigPtr->reserved      =  0;
	initConfigPtr->busNum        =  bus;
	initConfigPtr->deviceNum     =  device;
	initConfigPtr->functionNum	  =  0;
	initConfigPtr->registerNum   =  0;
	return initConfigPtr;
}

void checkDevice(uint8_t bus, uint8_t device) {
	uint32_t dat, resultado; 
	uint8_t clase;
	char *res;	
	int vend, prod, sub_clase, multifunc, func_dev;
	ConfigDir *configPtr;
	if ( (configPtr = init_check_device(bus, device)) == NULL) { 
		fprintf(stderr, "Error: No se pudo asignar memoria.\n"); 
		return ; 
	}

	// Construir el número de 32 bits
    resultado = construirNumero(configPtr);

	// Obtain vendor to check if device exists
	outl (resultado, CONFIG_DIR); 
	dat = inl(CONFIG_DAT);

	if (dat == 0xFFFFFFFF) {
		//fprintf(stderr, "no existe ese dispositivo\n"); 
		free(configPtr);
		return ; 
	}	
	vend = dat & 0x0000FFFF; prod = dat >> 16; // extrae vendedor y producto

	// Obtain if it is multifunction
    configPtr->registerNum = 3; // Puedes ajustar estos valores según tus necesidades
    // Construir el número de 32 bits
    resultado = construirNumero(configPtr);
	// Escribir direccion
	outl (resultado, CONFIG_DIR); 
	// Obtener data
	dat = inl(CONFIG_DAT);
	// Construye valor
	multifunc = ((dat >> 16) & 0x0080);

	// leer el registro 0x02
	configPtr->registerNum = 2;
		
	// Ver si es multifuncion
	if( (multifunc & 0x80) != 0) {
		
		for (func_dev = 1; func_dev < 8; func_dev++) {
			// Asignas el function number
			configPtr->functionNum	  =  func_dev;
			// Construir el número de 32 bits
			resultado = construirNumero(configPtr);
			// Acceso a los puertos
			outl (resultado, CONFIG_DIR); dat = inl(CONFIG_DAT); // extrae clase y subclase
			// Construye valor
			clase = dat >> 24; sub_clase = (dat >> 16) & 0x00FF;
			// Condicion sobre si es de almacenamiento y de subclase IDE, SATA o NVM
			if (clase == 0x01 && (sub_clase == 0x01 || sub_clase == 0x06 || sub_clase == 0x08)){
				switch (sub_clase) {
					case 0x01:
						res = "IDE";
						printf("Controlador de almacenamiento %s: Bus 0x%x Ranura 0x%x Func 0x%x: Vendedor %x Producto %x \n", res, bus, device, func_dev, vend, prod);
					break;
					case 0x06:
						res = "AHCI";
						// Accede al BAR5
						configPtr->registerNum = 9;
						// Construir el número de 32 bits
						resultado = construirNumero(configPtr);
						// Acceso a los puertos
						outl (resultado, CONFIG_DIR); dat = inl(CONFIG_DAT); // extrae clase y subclase
						// enviamos la direccion de 32 bits de BAR5
						mmio_access_ahci(dat);
						printf("Controlador de almacenamiento %s: Bus 0x%x Ranura 0x%x Func 0x%x: Vendedor %x Producto %x \n", res, bus, device, func_dev, vend, prod);
					break;
					case 0x08:
						res = "NVM";
						printf("Controlador de almacenamiento %s: Bus 0x%x Ranura 0x%x Func 0x%x: Vendedor %x Producto %x \n", res, bus, device, func_dev, vend, prod);
					break;
				}
				
				
			}
			
		}
	}else{
		
		// Construir el número de 32 bits
		resultado = construirNumero(configPtr);
		// Acceso a los puertos
		outl (resultado, CONFIG_DIR); dat = inl(CONFIG_DAT); // extrae clase y subclase
		// Construye valor
		clase = dat >> 24; sub_clase = (dat >> 16) & 0x00FF;
		// Condicion sobre si es de almacenamiento y de subclase IDE, SATA o NVM
		if (clase == 0x01 && (sub_clase == 0x01 || sub_clase == 0x06 || sub_clase == 0x08)){
			switch (sub_clase) {
				case 0x01:
					res = "IDE";
					printf("Controlador de almacenamiento %s: Bus 0x%x Ranura 0x%x Func 0x%x: Vendedor %x Producto %x \n", res, bus, device, 0, vend, prod);
				break;
				case 0x06:
					res = "AHCI";
					// Accede al BAR5
					configPtr->registerNum = 9;
					// Construir el número de 32 bits
					resultado = construirNumero(configPtr);
					// Acceso a los puertos
					outl (resultado, CONFIG_DIR); dat = inl(CONFIG_DAT); // extrae clase y subclase
					// enviamos la direccion de 32 bits de BAR 5
					printf("Controlador de almacenamiento %s: Bus 0x%x Ranura 0x%x Func 0x%x: Vendedor %x Producto %x \n", res, bus, device, 0, vend, prod);
					mmio_access_ahci(dat);
				break;
				case 0x08:
					res = "NVMe";
					printf("Controlador de almacenamiento %s: Bus 0x%x Ranura 0x%x Func 0x%x: Vendedor %x Producto %x \n", res, bus, device, 0, vend, prod);
				break;
			}
			
		}
	}
	
	// liberar memoriade del puntero inicializado en init_check_device()
	free(configPtr);
}


void checkAllBuses(void) {
     uint16_t bus;
     uint8_t device;
     for (bus = 0; bus < 256; bus++) {
         for (device = 0; device < 32; device++) {
             checkDevice(bus, device);
         }
     }
}

int main(int argc, char *argv[]) {
	if (ioperm(CONFIG_DIR, 8, 1) < 0) { // permiso para acceso a los 2 puertos modo usuario
                perror("ioperm"); return 1;
	}

	checkAllBuses();

	return 0;
} 
