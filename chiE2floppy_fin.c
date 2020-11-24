#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <linux/ext2_fs.h>
#include <ustat.h>

#define block_size 1024

char *buff_super_block;
char *buff_group_desc;

struct ext2_super_block *sblock; //Almacena superbloque
struct ext2_group_desc *grdesc; //Almacena descriptor de grupo
struct ext2_inode *array_inodes_table; //Almacena tabla de inodos
struct ext2_dir_entry *dirarr; //Almacena directorios
int *n_inodes_references; //Almacena referencias reales a cada inodo.
int n_inodes_count=0; //Lleva la cuenta real de inodos referenciados.

void get_inodes_bitmap(int, int*);
void get_dir_info(int,int);
void show_files_info(int, struct ext2_inode);
void get_files_references(int, struct ext2_inode, int*);
int check_inodes_consistency(char*, int);

int main(int argc, char *argv[])
{
    int op,i;    
    char* cad=argv[2];

    int fd = open(cad,O_RDONLY); //Se abre el fichero que representa el FS
    
    buff_super_block = (char *)malloc(sizeof(struct ext2_super_block));
    buff_group_desc = (char *)malloc(sizeof(struct ext2_group_desc));

    sblock = (struct ext2_super_block *)malloc(sizeof(struct ext2_super_block));
    grdesc = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));

    ///////////////////////////
    /*Lectura del superbloque*/
    ///////////////////////////

    lseek(fd,block_size,SEEK_SET); //Nos posamos al principio del superbloque
    read(fd,buff_super_block,sizeof(struct ext2_super_block)); //Leemos la estructura que representa al superbloque, depositamos en el buffer
    memcpy((void *)sblock,(void *)buff_super_block,sizeof(struct ext2_super_block)); //Copiamos lo leído que está
    
    if(sblock->s_magic==61267) //Verifica si el FS es o no ext2
        printf("\nEl sistema de archivos es ext2.\n\n");
    else
    {
        printf("El sistema de archivos no es ext2. Abortando ejecución...\n\n");
        exit(-1);
    }

    ///////////////////////////////////
    /*Lectura del descriptor de grupo*/
    ///////////////////////////////////

    lseek(fd,2*block_size,SEEK_SET); //Nos posamos al principio del bloque que representa el descriptor de grupo en el fichero
    read(fd,buff_group_desc,sizeof(struct ext2_group_desc)); //Leemos la estructura que representa al descriptor de grupo, depositamos en el buffer
    memcpy((void *)grdesc,(void *)buff_group_desc,sizeof(struct ext2_group_desc)); //Se copia el contenido del buffer

    /////////////////////////////////
    /*Lectura de la tabla de inodos*/
    /////////////////////////////////

    array_inodes_table=(struct ext2_inode *)malloc((sblock->s_inodes_count)*sizeof(struct ext2_inode *)); //Se reserva memoria para almacenar la tabla de inodes

    lseek(fd,(grdesc->bg_inode_table)*block_size,SEEK_SET); //Nos posamos al principio del bloque que representa la tabla de inodes del fichero
    read(fd,array_inodes_table,((sblock->s_inodes_count)*(sizeof(struct ext2_group_desc)))); //Leemos la estructura que representa al descriptor de grupo, depositamos en el buffer

    while((op= getopt(argc,argv,"dic")) !=-1) 	
	{
	   switch(op)
       {
            case 'd': get_dir_info(fd,0); break; // Número de inodo de cada entrada de directorio.
            case 'i': get_dir_info(fd,1); break; // Inodo de entrada de directorio + enlaces correspondientes.
            case 'c': check_inodes_consistency(cad,fd); break; // Consistencia de inodos.
            default: puts("Opción no válida seleccionada");
       }
    }
    
    //Liberación de memoria.

    free(sblock);
    free(grdesc);
    free(array_inodes_table);
    free(dirarr);
    free(n_inodes_references);

    return 0;
}

int check_inodes_consistency(char* cad, int fd)
{
    int i, consistency_flag=0;

    int inodes_count_bitmap;
    int* array_inodes_bitmap;

    array_inodes_bitmap = (int*)malloc((sblock->s_inodes_count)*sizeof(int*));
    
	printf("Cantidad total de inodos: %d\n\n", sblock->s_inodes_count);
    
    get_dir_info(fd,2); //Para obtener el conteo real de inodos referenciados.
    printf("Cantidad total de inodos segun referencias en area de datos: %d\n\n",n_inodes_count);

    printf("Cantidad de inodos ocupados segun superbloque: %d\n",(sblock->s_inodes_count)-(sblock->s_free_inodes_count));
    printf("Cantidad de inodos libres segun superbloque: %d\n\n",(sblock->s_free_inodes_count));

    get_inodes_bitmap(fd,array_inodes_bitmap); //Obtiene bitmap

    for(i=0;i<(sblock->s_inodes_count);i++)
    {
        if(array_inodes_bitmap[i]!=0)
            inodes_count_bitmap++;
    }     
    
    printf("Cantidad de inodos ocupados segun inode bitmap: %d\n",inodes_count_bitmap);
    printf("Cantidad de inodos libres segun inode bitmap: %d\n",(sblock->s_inodes_count)-(inodes_count_bitmap));
    
    if(sblock->s_free_inodes_count!=((sblock->s_inodes_count)-(inodes_count_bitmap))!=n_inodes_count)
    {
        //Comienza a contarse desde 10 por los inodos reservados.

        for(i=10;i<(sblock->s_inodes_count);i++)
        {
            if(array_inodes_table[i].i_links_count!=0 && array_inodes_bitmap[i]==0)
            {
                printf("Inodo %i referenciado en tabla y libre en bitmap.\n", i+1);
                consistency_flag=1;
            }
            if(array_inodes_table[i].i_links_count==0 && array_inodes_bitmap[i]!=0)
            {    
                printf("Inodo %i no referenciado en tabla y ocupado en bitmap.\n", i+1);
                consistency_flag=1;
            }
            if(n_inodes_references[i+1]!=0 && array_inodes_bitmap[i]==0)
            {
                printf("Inodo %i referenciado en directorio y libre en bitmap.\n", i+1);
                consistency_flag=1;
            }
            if(n_inodes_references[i+1]==0 && array_inodes_bitmap[i]!=0)
            {
                printf("Inodo %i no referenciado en directorio y marcado en bitmap.\n", i+1);
                consistency_flag=1;
            }
            if(n_inodes_references[i+1]!=0 && array_inodes_table[i].i_links_count==0)
            {
                printf("Inodo %i referenciado en directorio y sin referencias en tabla de inodos.\n", i+1);
                consistency_flag=1;
            }
            if(n_inodes_references[i+1]==0 && array_inodes_table[i].i_links_count!=0)
            {
                printf("Inodo %i no referenciado en directorio y con referencias segun tabla.\n", i+1);
                consistency_flag=1;
            }
            
        }   
    }
    
    if(consistency_flag==0)
        printf("\n\nEl sistema de archivos es consistente.\n");
    else
        printf("\n\nEl sistema de archivos es inconsistente.\n");
}

void get_inodes_bitmap(int fd, int* array_inodes_bitmap)
{
    int i,j,k,div,bin[8];
    __u8 *inodes_bitmap;

    inodes_bitmap = (__u8*)malloc((sblock->s_inodes_count)*sizeof(__u8*)/8);
    
	lseek(fd,(((grdesc->bg_inode_bitmap))*block_size),SEEK_SET); //Se posiciona al inicio del bitmap
	read(fd,inodes_bitmap,((sblock->s_inodes_count)*sizeof(__u8)/8));  
	
    //Algoritmo para pasar el contenido de cada byte a bits y ver si está en 0 o 1 y sumarlo al contador
    
    for(i=0;i<(sblock->s_inodes_count)/8;i++)
	{
        div=inodes_bitmap[i];
        
        for(j=0;j<8;j++)
            bin[j]=0;

        for(j=0;j<8 && div!=0;j++)
        {
            bin[j]=div%2;
            div=div/2;
        }

        for(j=0;j<8;j++)
        {   
            if(bin[j]==1)
                array_inodes_bitmap[j+(i*8)]=bin[j];
        }
    }
}

void get_dir_info(int fd, int mode)
{
    //Se accede al segundo inodo de la lista, 
    //que es el que corresponde al directorio raíz.
    
    struct ext2_inode root_inode=array_inodes_table[1];
    int i,j,j_count,char_count; 
    
    //i, j son contadores. 
    //j_count lleva la cuenta para un bucle interno. 
    //char_count se usa para calcular la cantidad de caracteres por línea.

    //Recordar ignorar los inodos para i del 0 al 9, 
    //ya que están reservados los primeros 10.
    
    //Ignorar el inodo i=10 para mostrar su contenido 
    //dado que pertenece a lost+found.

    //MODE == 0: realiza la opción -d.
    //MODE == 1: realiza la opción -i.
    //MODE == 2: provee el conteo de referencias reales segun área de datos.

    if(mode==0)
    {
        printf("El inodo 2 referencia al directorio raiz\n\n");
        printf("DIRECTORIO | INODO CORRESPONDIENTE\n");
        show_files_info (fd,root_inode);

        for(i=11;i<((sblock->s_inodes_count)-(sblock->s_free_inodes_count));i++)
        {
            if(16000 <= array_inodes_table[i].i_mode && array_inodes_table[i].i_mode < 17000)
            {
                printf("El inodo %d referencia un directorio.\n\n",i+1,array_inodes_table[i].i_mode);
                printf("DIRECTORIO | INODO CORRESPONDIENTE\n");
                show_files_info(fd,array_inodes_table[i]);
            }        
        }
    }
    if(mode==1)
    {
 
        n_inodes_references=(int*)malloc(sizeof(int)*(sblock->s_inodes_count));
        
        for(i=0;i<sblock->s_inodes_count;i++)
            n_inodes_references[i]=0;
        
        get_files_references(fd, array_inodes_table[1], n_inodes_references);

        for(i=11;i<((sblock->s_inodes_count)-(sblock->s_free_inodes_count));i++)
        {
            if(16000 <= array_inodes_table[i].i_mode && array_inodes_table[i].i_mode < 17000)
            {
                get_files_references(fd, array_inodes_table[i], n_inodes_references);
            }        
        }
        
        printf("NUMR. INODO:    ");
        char_count=strlen("NUMR. INODO:    ");

        j_count=0;
        
        for(i=0;i<sblock->s_inodes_count;i++)
        {
            if(n_inodes_references[i]!=0)
            {
                if(i<10)
                    printf("00%d ",i);
                if(i>=10 && i<100)
                    printf("0%d ",i);
                if(i>=100)
                    printf("%d ",i);
                
                char_count+=4;

                if (char_count>=80 || i>=((sblock->s_inodes_count)-(sblock->s_free_inodes_count)))
                {
                    char_count=strlen("NUMR. INODO:    ");
                    
                    if(i>=((sblock->s_inodes_count)-(sblock->s_free_inodes_count)))
                        printf("\n");

                    printf("REFERENCIAS:    ");
                    
                    for(j=j_count;j<sblock->s_inodes_count;j++)
                    {
                        if(n_inodes_references[j]!=0)
                        {
                            if(n_inodes_references[j]<10)
                                printf("00%d ",n_inodes_references[j]);
                            if(n_inodes_references[j]>=10 && n_inodes_references[j]<100)
                                printf("0%d ",n_inodes_references[j]);
                            if(n_inodes_references[j]>=100)
                                printf("%d ",n_inodes_references[j]);
                            
                            char_count+=4;
                        }    
                        
                        j_count++;

                        if (char_count>=80)
                        {
                            char_count=strlen("NUMR. INODO:    ");
                            printf("\n\nNUMR. INODO:    ");
                            break;
                        }    
                    }           
                }
            }    
        }
        printf("\n");
    }   

    if(mode==2)
    {
        n_inodes_references=(int*)malloc(sizeof(int)*(sblock->s_inodes_count));
        
        for(i=0;i<sblock->s_inodes_count;i++)
            n_inodes_references[i]=0;
        
        get_files_references(fd, array_inodes_table[1], n_inodes_references);

        for(i=11;i<((sblock->s_inodes_count)-(sblock->s_free_inodes_count));i++)
        {
            if(16000 <= array_inodes_table[i].i_mode && array_inodes_table[i].i_mode < 17000)
            {
                get_files_references(fd, array_inodes_table[i], n_inodes_references);
            }        
        }
        
        for(i=10;i<sblock->s_inodes_count;i++)
        {
            if(n_inodes_references[i]!=0)
                n_inodes_count++;     
        }
        
        n_inodes_count+=10; //Se suma 10 por los inodos reservados.
    }  
}

void get_files_references(int fd, struct ext2_inode inode, int* inodes_n_references)
{
    struct ext2_dir_entry_2 dir_info;
    int jump=0, i, size=0;
    
    //Ubicación del bloque que corresponde a la información del 
    //directorio a mostrar en el archivo del FS.

    lseek(fd,inode.i_block[0]*block_size,SEEK_SET);
    read(fd,&dir_info,sizeof(struct ext2_dir_entry_2));

    if(inode.i_size>0)
    {
        do
        {    
            lseek(fd,inode.i_block[0]*block_size+jump,SEEK_SET);
    
            //Se lee el contenido del directorio.
            read(fd,&dir_info,sizeof(struct ext2_dir_entry_2));

            inodes_n_references[dir_info.inode]++;

            jump+=dir_info.rec_len; 
            size+=dir_info.rec_len; 

        }while(size<inode.i_size);
    }
}

void show_files_info(int fd, struct ext2_inode inode)
{
    struct ext2_dir_entry_2 dir_info;
    int jump=0, i, size=0;
    
    //Ubicación del bloque que corresponde a la información del 
    //directorio a mostrar en el archivo del FS.

    lseek(fd,inode.i_block[0]*block_size,SEEK_SET);
    read(fd,&dir_info,sizeof(struct ext2_dir_entry_2));

    if(inode.i_size>0)
    {
        do
        {    
            lseek(fd,inode.i_block[0]*block_size+jump,SEEK_SET);
    
            //Se lee el contenido del directorio.
            read(fd,&dir_info,sizeof(struct ext2_dir_entry_2));
            
            for(i=0;i<dir_info.name_len;i++)
                printf("%c",dir_info.name[i]);
            
            printf("    ");
            printf("%i",dir_info.inode);
            printf("\n");

            jump+=dir_info.rec_len; 
            size+=dir_info.rec_len;
            
            //Jump es el salto en bytes hasta ubicar el próximo archivo.
            //Size mide el tamaño del record length, 
            //a fin de no superar el tamaño del directorio.

        }while(size<inode.i_size);
    }
    printf("\n");
}
