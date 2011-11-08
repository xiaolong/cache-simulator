/* CS538 HW2, Cache Simulator, by Xiaolong Cheng */
/* Build environment: Ubuntu 10.4, with g++ */

/* Build version 1.4  */

#include<cstdio>  
#include<cstdlib>
#include<cstring>
#include<cmath>
#include<stack>
#include<string>
#include<iostream>
using namespace std;

#define Bytes_Per_Line 32
#define Num_Sets 64
#define Lines_Per_Set 4
#define Tag_Size 21
#define Overhead_Bits 25
#define Num_Lines 256
#define Stream_Cycles 20
#define Normal_Access_Cycles 1
//#define Bits_Per_Word 32
//#define Bytes_Per_Word 4
#define Size_Per_Line Bytes_Per_Line

/*use parentheses!! buggy otherwise*/
#define Size_Per_Set (Size_Per_Line*Lines_Per_Set)
#define Cache_Size (Size_Per_Set*Num_Sets)

// by default, debug, trace and version command are all disabled
bool _DEBUG = false;
bool _TRACE = false;
bool _VERSION = false;

/* cache_status is a struct to keep track of all the statistics of the cache
   it is updated by many functions */
struct cache_status{
  int access_count;  //number of memory refs
  int read_count; //number of reads
  int write_count; //number of writes
  int cycles_with_cache; 
  int cycles_without_cache;
  int stream_in_count;
  int stream_out_count;
  int replacement_count;//number of replacements(or overwrites)
  int miss_count;
  int hit_count;
  int read_hit_count;
  int write_hit_count;
  int write_back_count; //number of write-backs(stream out dirty lines)
  double hit_rate;
} status ={0,0,0,0,0,0,0,0,0,0,0,0,0,0.0};

/* this function prints the cache statistics(status)  */
void printStatus(){
  /* prepare data before printing */
  status.access_count=status.read_count+status.write_count;
  status.cycles_with_cache=status.access_count+ (status.stream_in_count + status.stream_out_count)*20;
  status.cycles_without_cache= status.access_count * Stream_Cycles;
  status.hit_count=status.read_hit_count + status.write_hit_count;
  
  status.hit_rate=(double) status.hit_count/status.access_count;
  //TODO: more logic here
  printf("--------------STATISTICS---------------\n");
  printf("access_count:%d (read_count:%d, write_count:%d)\n", status.access_count, status.read_count, status.write_count);
  printf("cycles_with_cache: %d \n", status.cycles_with_cache);
  printf("cycles_without_cache: %d\n",status.cycles_without_cache);
  printf("stream_in_count: %d\n",status.stream_in_count);
  printf("stream_out_count: %d\n",status.stream_out_count);
  printf("replacement_count: %d\n",status.replacement_count);
  printf("miss_count: %d\n",status.miss_count);
  printf("hit_count: %d (read_hit: %d, write_hit: %d)\n",status.hit_count, status.read_hit_count, status.write_hit_count);
  printf("write_back_count: %d\n",status.write_back_count);
  printf("HIT RATE : %f \n", status.hit_rate);
  printf("-----------------END-----------------\n");

}


stack<string> debugInfo; // LIFO for debug information

/* this class simulates data structure of a single line */
class cacheline{
public:
  char tag[Tag_Size];
  bool valid;
  bool dirty;
  int lru; //Yes!! LRU is implemented as Int, but will mod 4!!

  /* real data is not used in simulation,
     so I comment out them to save space */
  //int data[4]; 

  cacheline(){//constructor
    for(int i=0;i<Tag_Size;++i)tag[i]='x';//initialize tag to random chars
    valid=false;
    dirty=false;
    lru=0;
  }
};

/* compare if 2 char[] are equal in first N chars,
 * this function is used to compare tags */
bool charEqual(char* a, char* b, int N){
  int count=0;
  for(int i=0;i<N;++i){
    if(a[i]==b[i])count++;
  }
  return count==N;
}

/* this class contains the data structure to simulate a set, 
 *LRU policy and eviction algorithm are implemented here */
class set{
public:
  cacheline* cachelines[Lines_Per_Set];

  set(){//constructor
    for(int i=0;i<Lines_Per_Set;++i){
      cachelines[i]=new cacheline();
    }
  }  

  //this function updates LRU bits of lines, given the hitted line index
  void updateLRU(int hit){
    for(int i=0;i<Lines_Per_Set;++i){
      if(cachelines[i]->valid){//increase age of all valid lines
	cachelines[i]->lru++;
	cachelines[i]->lru = cachelines[i]->lru% Lines_Per_Set;
	/*BUG resolved: I used to write  
	  cachelines[i]->lru= (cachelines[i]->lru++)%Lines_Per_Set; 
	  but it didn't give the right answer. dont know why.. */
      }
    }
    cachelines[hit]->lru=0;//clear the age of hitted line
  }
  
  /* stream a paragraph into this set, given the address */
  void streamIn(char* addrBin){ 
    
    //find the first empty line to stream in paragraph
    int choice;//use variable to track the selected line
    for(int i=0;i<Lines_Per_Set;++i){
      if(cachelines[i]->valid==false){
	choice = i;
	break;
      }//end-if
    }//end-for
    cachelines[choice]->valid=true;
    cachelines[choice]->dirty=false;
    for(int j=0;j<Tag_Size;++j) cachelines[choice]->tag[j]=addrBin[j];
    //printf("tag is %s \n", cachelines[choice]->tag);
    updateLRU(choice);
    status.stream_in_count++;
  }

  /* this function increases the stream_out_count,
   * Function only called by evictAndLoad() */
  void streamOut(int lineIndex){
    /* some stream out operation should be here, but here
     * I didn't implement because I only care about simulation here
     */
    status.stream_out_count++;
    if(_TRACE) fprintf(stdout,"stream out...\n");
  }
  
  /*stream in a new paragraph without stream out */
  void replaceLine(int lineIndex, char* addrBin){
    cachelines[lineIndex]->valid=true;
    cachelines[lineIndex]->dirty=false;
    for(int j=0;j<Tag_Size;++j) cachelines[lineIndex]->tag[j]=addrBin[j];
    updateLRU(lineIndex);
    status.stream_in_count++;
    status.replacement_count++;//count the replacements
    if(_TRACE)fprintf(stdout,"replace/stream in...\n");
  }

  void evictAndLoad(char* addrBin){//problematic!!!!!!!!!!
    int choice;
    /*the loop finds the oldest line in a set*/
    for(int i=0;i<Lines_Per_Set;++i){
      if(cachelines[i]->lru==3){
	choice=i;
	break;
      };
    }//end-for
    //printf("choice is %d\n", choice);
    if(cachelines[choice]->dirty){//if dirty
      //stream out then stream in
      streamOut(choice);
      status.write_back_count++;//count the write-backs
      replaceLine(choice,addrBin);
    }else{//if not dirty
      //stream in to replace
      replaceLine(choice,addrBin);
    }
  }


  /* setDirty() sets a line's dirty bit, given the address to write.
   * This function is only used by write() in cache class */
  void setDirty(char* addrBin){
    for(int i=0;i<Lines_Per_Set;++i){
      if(charEqual(cachelines[i]->tag, addrBin, Tag_Size)){
	cachelines[i]->dirty = true;
      }
    }//end-for
  }

};

// this function is only used in debugging....
/*this function converts an Int to binary form in char[] 
 * EXAMPLE:  input: 4;  output: "...00100"   */
char* DecToBin(int addrDec){
  char* addrBin = new char[32];//or 33??
  for(int i=0;i<32;++i){
    if( (addrDec & (1<<i))>>i) addrBin[31-i]='1';
    else addrBin[31-i]='0';
  }
  return addrBin;
}

/* hexToBin() converts hex style into binary style, both in string type 
 * this function is fat.. i hope i can come up with a cleaner one..*/
char* hexToBin(char* input){ 
    int x = 4;
    int size;
    size = strlen(input);
    //printf("size is %d\n",size);

    char* output = new char[size*4+1];
   
    for (int i = 0; i < size; i++)
    {
        if (input[i] =='0') {
            output[i*x +0] = '0';
            output[i*x +1] = '0';
            output[i*x +2] = '0';
            output[i*x +3] = '0';
        }
        else if (input[i] =='1') {
            output[i*x +0] = '0';
            output[i*x +1] = '0';
            output[i*x +2] = '0';
            output[i*x +3] = '1';
        }    
        else if (input[i] =='2') {
            output[i*x +0] = '0';
            output[i*x +1] = '0';
            output[i*x +2] = '1';
            output[i*x +3] = '0';
        }    
        else if (input[i] =='3') {
            output[i*x +0] = '0';
            output[i*x +1] = '0';
            output[i*x +2] = '1';
            output[i*x +3] = '1';
        }    
        else if (input[i] =='4') {
            output[i*x +0] = '0';
            output[i*x +1] = '1';
            output[i*x +2] = '0';
            output[i*x +3] = '0';
        }    
        else if (input[i] =='5') {
            output[i*x +0] = '0';
            output[i*x +1] = '1';
            output[i*x +2] = '0';
            output[i*x +3] = '1';
        }    
        else if (input[i] =='6') {
            output[i*x +0] = '0';
            output[i*x +1] = '1';
            output[i*x +2] = '1';
            output[i*x +3] = '0';
        }    
        else if (input[i] =='7') {
            output[i*x +0] = '0';
            output[i*x +1] = '1';
            output[i*x +2] = '1';
            output[i*x +3] = '1';
        }    
        else if (input[i] =='8') {
            output[i*x +0] = '1';
            output[i*x +1] = '0';
            output[i*x +2] = '0';
            output[i*x +3] = '0';
        }
        else if (input[i] =='9') {
            output[i*x +0] = '1';
            output[i*x +1] = '0';
            output[i*x +2] = '0';
            output[i*x +3] = '1';
        }
        else if (input[i] =='a') {    
            output[i*x +0] = '1';
            output[i*x +1] = '0';
            output[i*x +2] = '1';
            output[i*x +3] = '0';
        }
        else if (input[i] =='b') {
            output[i*x +0] = '1';
            output[i*x +1] = '0';
            output[i*x +2] = '1';
            output[i*x +3] = '1';
        }
        else if (input[i] =='c') {
            output[i*x +0] = '1';
            output[i*x +1] = '1';
            output[i*x +2] = '0';
            output[i*x +3] = '0';
        }
        else if (input[i] =='d') {    
            output[i*x +0] = '1';
            output[i*x +1] = '1';
            output[i*x +2] = '0';
            output[i*x +3] = '1';
        }
        else if (input[i] =='e'){    
            output[i*x +0] = '1';
            output[i*x +1] = '1';
            output[i*x +2] = '1';
            output[i*x +3] = '0';
        }
        else if (input[i] =='f') {
            output[i*x +0] = '1';
            output[i*x +1] = '1';
            output[i*x +2] = '1';
            output[i*x +3] = '1';
        }
    }

    output[32] = '\0';
    //printf("strlen of output is %d\n",strlen(output));
    return output;
}



/* this function returns the setIndex, given an requested address */
int getSetIndex(char* addrBin){
  int tmp=0;
  for(int i=0;i<6;++i){
    if(addrBin[Tag_Size+i]=='1') tmp+= pow(2,5-i);
  }
  return tmp;
}

/** the cache class is the entry for cache operations **/
class cache{
public:
  set* sets[Num_Sets];

  cache(){//constructor
    for(int i=0;i<Num_Sets;++i) sets[i]= new set();
  }

  /*function isHit() matches address with cache directory(tags)*/
  bool isHit(char* addrBin, int setIndex){
      for(int j=0;j<Lines_Per_Set;++j){
	if (charEqual(sets[setIndex]->cachelines[j]->tag, addrBin, Tag_Size)&& sets[setIndex]->cachelines[j]->valid){
	  if(_TRACE)fprintf(stdout,"hit on set %d line %d\n",setIndex, j);
	  return true;
	}
      }//end-for
    return false;
  }

  /* this function checks if there is empty line(s) within a given set */
  bool emptyLineAvailable(int setIndex){
    for(int i=0;i<Lines_Per_Set;++i){
      if (sets[setIndex]->cachelines[i]->valid == false) return true;
    }
    return false;
  }
  
  /*this function implements the logic to handle read request */
  void read(char* addrBin){
    int setIndex = getSetIndex(addrBin);
    if(_TRACE)fprintf(stdout,"reading set %d...\n ", setIndex);
    status.read_count++;
    if(isHit(addrBin, setIndex)){
      status.read_hit_count++;
      //read here..
    } else { // what about miss?
      status.miss_count++;
      if(emptyLineAvailable(setIndex)){// still got space?
	if(_TRACE)fprintf(stdout,"R missed, space available, stream in \n");
	sets[setIndex]->streamIn(addrBin);//stream in
      } else { // what about full?
	if(_TRACE)fprintf(stdout,"R missed and full, do eviction\n");
	sets[setIndex]->evictAndLoad(addrBin); //stream out and stream in	
      }
    }

    if(_DEBUG){// print debug info if activated, ONLY valid lines will be print
      for(int i=0;i<Num_Sets;++i){
	for(int j=0;j<Lines_Per_Set;++j){
	  if(sets[i]->cachelines[j]->valid){
	    char buffer[80];
	    sprintf(buffer, "Set:%d; Line:%d; Dirty:%d; Valid:%d; LRU:%d; Tag:%s\n",i,j,sets[i]->cachelines[j]->dirty, sets[i]->cachelines[j]->valid, sets[i]->cachelines[j]->lru, sets[i]->cachelines[j]->tag);
	    debugInfo.push(buffer);
	    }
	}
      }
    }
  }

  /*this function implements the logic to handle write requests*/
  void write(char* addrBin){
    int setIndex = getSetIndex(addrBin);
    if(_TRACE)fprintf(stdout,"writing set %d...\n",setIndex);
    status.write_count++;
    if(isHit(addrBin, setIndex)){
      status.write_hit_count++;
      //write here..
      sets[setIndex]->setDirty(addrBin);//SET DIRTY
    }else{//what if miss?
      status.miss_count++;
      if(emptyLineAvailable(setIndex)){// got space?
	//stream in
	if(_TRACE)fprintf(stdout,"W missed but not full, stream in\n ");
	sets[setIndex]->streamIn(addrBin);
	sets[setIndex]->setDirty(addrBin);//SET DIRTY
      }else {//what if full?
	if(_TRACE)fprintf(stdout,"W missed and full, do eviction\n");
	sets[setIndex]->evictAndLoad(addrBin);
	//some store operation
	sets[setIndex]->setDirty(addrBin);//SET DIRTY

      }//end-if-else
    }//end-if-else
    
    if(_DEBUG){// print debug info if activated, ONLY valid lines will be print
      for(int i=0;i<Num_Sets;++i){
	for(int j=0;j<Lines_Per_Set;++j){
	  if(sets[i]->cachelines[j]->valid)  {
	    char buffer[80];
	    sprintf(buffer,"Set:%d; Line:%d; Dirty:%d; Valid:%d; LRU:%d; Tag:%s\n",i,j,sets[i]->cachelines[j]->dirty, sets[i]->cachelines[j]->valid, sets[i]->cachelines[j]->lru, sets[i]->cachelines[j]->tag);
	    debugInfo.push(buffer);
	  }
	}
      }
      }

  }//end-func

};


/* The driver creates and drives the cache. It scans through the input
 * and extract the r/w instructions to drive the cache, create statistics
 * It reports errors if input file is not valid or contains errors  */
class driver{
  char* fileString;//to store filename passed with command
public:
  driver(char* arg){
    fileString = arg;
  }

 void process(){
   //reopen and redirect stdin to the file
   FILE* inFile = freopen(fileString,"r",stdin);

   /*to read from stdin without redirecting, just remove the line above
    * and use 'stdin' to replace the 'inFile' in the line below
    * I redirected to the file since I am more used to this way */
   if(inFile==NULL)printf("INVALID FILE, TRY AGAIN! \n \n");
    else{
      printf("INPUT FILE SUCCESSFULLY LOADED \n");
      cache* myCache = new cache();

      while(true){
	char temp[2]; //to store r/w indicator
	fscanf(stdin,"%s",temp);//scan them into temp[]
	if(!strcmp(temp,"-v")){
	  _VERSION=true;
	  //printf("VERSION>>>\n");
	  temp[0]=' '; //the temp need to be cleared, otherwise infinite loop!
	  continue;
	} 
	if(!strcmp(temp,"-d")){
	  _DEBUG=true;
	  //printf("DEBUG>>\n");
	  temp[0]=' ';
	  continue;
	}
	if(!strcmp(temp,"-t")){
	  _TRACE = true;
	  //printf("TRACE>>\n");
	  temp[0]=' ';
	  continue;
	}
	char temp2[10];//to store address starting with 0x..
	fscanf(stdin,"%s",temp2);
	//printf("size of temp: %d\n",sizeof(temp));
	if(feof(stdin)) break;
	/*kind of 'hack' here --  I extracted the third
	 * char through the eighth from the address, which 
	 * is the real address, the head "0x" is dumped... */
	if(!strcmp(temp,"r")||!strcmp(temp,"R")){
	  if(_TRACE)printf("reading from address: %s\n",temp2);
	  myCache->read(hexToBin(&temp2[2]));
	}
	else if(!strcmp(temp,"w")||!strcmp(temp,"W")){
	  if(_TRACE)printf("writing to address: %s\n",temp2);
	  myCache->write(hexToBin(&temp2[2]));
	}
	//report strange character in input if found
	//-t -v -d are skipped before,so don't need to care about them here
	else {
	  printf("STRANGE CHAR! CHECK YOUR INPUT!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	  return;//exit prog
	}
	      
      }//end-while-loop

      fclose(stdin);//close the stdin stream
      if(_VERSION)printf("[[- BUILD VERSION: 1.4 -]]\n");
      if(_DEBUG)printf("[[- DEBUG ACTIVATED -]]\n");
      if(_TRACE)printf("[[- TRACE ACTIVATED -]]\n");
      printStatus();
      }//end-else

   /* following block prints out debug info in LIFO order!!  */
   if(_DEBUG)printf("BELOW IS DEBUG INFORMATION IN LIFO ORDER\n");
   while(!debugInfo.empty()){
     if(_DEBUG)cout<<debugInfo.top();
     debugInfo.pop();
   }
   
 }//end-function

};


int main(int argc, char* argv[]){
  //set up Version, Trace, Debug if entered from keyboard
  for(int n=1;n<argc;++n){
    if(strcmp(argv[n],"-d")==0) _DEBUG = true;
    if(strcmp(argv[n],"-v")==0) _VERSION = true;
    if(strcmp(argv[n],"-t")==0) _TRACE = true;
  }//end-for

  /*feed the input file(last arg) into the driver for initialization*/
  driver* myDriver = new driver(argv[argc-1]); 
  myDriver->process();



  //code used for debugging lru..
   // printf("set 0 LRUs: %d / %d / %d / %d \n", myCache->sets[0]->cachelines[0]->lru, myCache->sets[0]->cachelines[1]->lru, myCache->sets[0]->cachelines[2]->lru, myCache->sets[0]->cachelines[3]->lru);
  

}
