#include <fcntl.h>   
#include <sys/stat.h> 
#include <unistd.h>
#include <sys/mman.h> 
#include <stdio.h>

#include "disk_driver.h"

void DiskDriver_init(DiskDriver* disk, const char* filename, int num_blocks){
	if(disk == NULL || filename == NULL || num_blocks < 1){
		printf("ERRORE: parametri non validi\n");
		return;
	}
	
	int bitmap_size = num_blocks / 8;	// Dimensione bitmap
	if(num_blocks % 8) ++bitmap_size;	// Arrotondo
	
	int fd;	// File descriptor da restituire
	// Controllo se posso accedere al filename e accedo / creo
	int can_access = access(filename, F_OK) == 0; 
	
	if(can_access){
		fd = open(filename, O_RDWR, (mode_t)0666);
		if(!fd){
			printf("Errore nell'apertura del file\n");
			return;
		}
	}
	else{
		fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, (mode_t)0666);
		if(!fd){
			printf("Errore nella creazione del file\n");
			return;
		}
		
		// Alloco il nuovo disco
		if(posix_fallocate(fd, 0, sizeof(DiskHeader) + bitmap_size)>0){
			printf("Errore nell'allocazione del disco");
		}
	}

	// Devo mappare il disco in memoria da restituire
	DiskHeader* disco_mappato = (DiskHeader*) mmap(0,
													sizeof(DiskHeader) + bitmap_size,
													PROT_READ | PROT_WRITE,
													MAP_SHARED,
													fd,
													0); 
	if(disco_mappato == MAP_FAILED){
		close(fd);
		printf("ERROR: mmap()\n");
		return;
	}
	
	// Lo setto 
	if(!can_access){
		disco_mappato->num_blocks = num_blocks;
		disco_mappato->bitmap_blocks = num_blocks;
		disco_mappato->bitmap_entries = bitmap_size;
		
		disco_mappato->free_blocks = num_blocks;
		disco_mappato->first_free_block = 0;
	}

	// Lo salvo
	disk->header = disco_mappato;
	disk->bitmap_data = (char*)disco_mappato + sizeof(DiskHeader);
	disk->fd = fd;
}
