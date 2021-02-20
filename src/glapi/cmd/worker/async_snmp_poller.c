/*
 some sort of Glaber copyright as well as paid add goes here
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include "zbxjson.h"

#ifdef HAVE_WINSOCK_H
#include <winsock.h>
#endif

/*****************************************************************************/
#define MAX_CONNECTIONS 4096
#define MAX_TASKS  1024*1024
void readTasks(tasks *Tas) {

}

int main (int argc, char **argv)
{
  initialize();
  

  while (1) {
      //global cycle
      //#1 reading new tasks if they are and there are free slots
      readTasks();
      //#2 starting new connections
      startConnections();
      //#3 processing responces if they are
      
      //#4 checking for timeouts
      

  }



  while ((read = getline(&line, &len, stdin)) != -1) {
    
    printf("Retrieved line of length %zu :\n", read);
    printf("%s", line);

    //parsing the line 

  
  }

  free(line);


  asynchronous();

  return 0;
}
