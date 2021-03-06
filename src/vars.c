/*************************************
 * C02 Variable Management Routines  *
 *************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "files.h"
#include "asm.h"
#include "parse.h"
#include "label.h"
#include "vars.h"

/* Lookup variable name in variable table  *
 * Sets: varidx = index into varnam array  *
 *                varcnt if not found      *
 * Returns: TRUE if found, otherwise FALSE */
int fndvar(char *name) {
  DEBUG("Looking up variable '%s'\n", name)
  for (varidx=0; varidx<varcnt; varidx++) 
    if (strcmp(varnam[varidx], name) == 0) return TRUE;
  return FALSE;
}

/* Lookup structure name in struct table   *
 * Sets: stcidx = index into sctnam array  *
 *                sctcnt if not found      *
 * Returns: TRUE if found, otherwise FALSE */
int fndstc(char *name) {
  DEBUG("Looking up struct '%s'\n", name)
  for (stcidx=0; stcidx<stccnt; stcidx++) 
    if (strcmp(strcts[stcidx].name, name) == 0) return TRUE;
  return FALSE;
}

/* Lookup structure member name in table   *
 * Sets: stmidx = index into stmnam array  *
 *                stmcnt if not found      *
 * Returns: TRUE if found, otherwise FALSE */
int fndmbr(int idx, char *name) {
  DEBUG("Looking up member '%s'\n", word)
  for (mbridx=0; mbridx<mbrcnt; mbridx++) {
    if (membrs[mbridx].strcti != idx) continue;
    if (strcmp(membrs[mbridx].name, name) == 0) return TRUE;
  }
  return FALSE;
}

/* Check for variable                       *
 * Generates error if variable is undefined *
 * Args: alwreg - allow register name       *
 *       alwcon - allow const variable      *
 *       name - variable name               */
void chksym(int alwreg, int alwcon, char *name) {
  if (strlen(name) == 1 && strchr("AXY", name[0])) {
    if (alwreg && valtyp != ARRAY) { 
      valtyp = REGISTER;
  	  return;
    }
    ERROR("Illegal reference to register %s\n", name, EXIT_FAILURE)
  }
  if (!fndvar(name))
    ERROR("Undeclared variable '%s' encountered\n", name, EXIT_FAILURE)
  if (!alwcon && (varmod[varidx] & MTCONST)) 
    ERROR("Illegal use of const variable '%s'\n", name, EXIT_FAILURE)
}

/* Process struct member *
 * Args: offset - Offset into Top Struct 
 *	     name - Struct Variable */
void prcmbr(char* name) {
  expect('.'); //Check for and Skip Period
  if (stcidx < 0) ERROR("Variable %s is Not a Structure\n", value, EXIT_FAILURE)
  getwrd(); //Get Member Name
  valtyp = gettyp(); //Determine Variable Type
  if (valtyp == FUNCTION) ERROR("Illegal Function Reference\n", 0, EXIT_FAILURE)  
  DEBUG("Checking for member %s", word) DETAIL(" with struct index %d\n", stcidx)
  if (!fndmbr(stcidx, word)) ERROR("Struct does Not Contain Member %s\n", word, EXIT_FAILURE)
  mbrofs += membrs[mbridx].offset; //Get Member Offet in Struct
}

/* Parse next word as struct member *
 * Args: name - Struct Variable    *
 * Sets: name - Variable + Offset  *
 *       valtyp - Member Type       */
void prsmbr(char* name) {
  mbrofs = 0;
  stcidx = varstc[varidx]; //Get Struct Index
  prcmbr(name);
  while (membrs[mbridx].stype == STRUCTURE && nxtchr == '.') {
    stcidx = membrs[mbridx].symidx;    
    prcmbr(membrs[mbridx].name);
  }
  sprintf(word, "+$%hhX", mbrofs); //Get Member Offet in Struct  
  strcat(name, word); //Add Offset to Struct  
}

/* Parse next word as variable or function name *
 * Args: alwreg - Allow Register Names          *
 * Sets: value - Identifier Name                *
 *       valtyp - Identifier Type               */
void prsvar(int alwreg, int alwcon) {
  getwrd(); //Get Variable Name
  valtyp = gettyp(); //Determine Variable Type
  if (valtyp != FUNCTION) chksym(alwreg, alwcon, word);
  strcpy(value, word);
  DEBUG("Parsed variable '%s'\n", value)
  if (valtyp == VARIABLE && match('.')) prsmbr(value);
}

/* Require and Parse Variable Name                         *
 * Parameters: alwary - Allow Array Reference *
 * Sets: vrname - operand for LDA/STA/LDY/STY */
void reqvar(int alwary) {
  prsvar(FALSE, TRUE);
  if (!alwary && valtyp != VARIABLE) expctd("Variable");
} 

/* Parse IndexOf Operator                    *
 * Sets: value - variable size (as string)  * 
 * Returns: variable size (as integer       */
int pidxof(void) { 
  expect('?');   //Check for and Skip SizeOf Operator
  DEBUG("Parsing IndexOf operator", 0);
  mbridx = -1; //Set Member Index to None
  reqvar(FALSE); //Parse Variable Name to get Size Of
  if (mbridx > -1) {
    sprintf(value, "$%hhX", membrs[mbridx].offset);
    return membrs[mbridx].offset;
  }
  ERROR("IndexOf operator requires a struct member\n", 0, EXIT_FAILURE);
  return 0; //Suppress Warning
}

/* Parse SizeOf Operator                    *
 * Sets: value - variable size (as string)  * 
 * Returns: variable size (as integer       */
int psizof(void) { 
  expect('@');   //Check for and Skip SizeOf Operator
  DEBUG("Parsing SizeOf operator", 0);
  mbridx = -1; //Set Member Index to None
  reqvar(FALSE); //Parse Variable Name to get Size Of
  if (mbridx > -1) {
    sprintf(value, "$%hhX", membrs[mbridx].size);
    return membrs[mbridx].size;
  }
  if (datlen[varidx]) {
    sprintf(value, "$%hhX", datlen[varidx]);
    return datlen[varidx];
  }
  if (strlen(varsiz[varidx]) == 0) {
    strcpy(value,"1");
    return 1;
  }
  strcpy(value, varsiz[varidx]);
  if (strcmp(value, "*") == 0) ERROR("Unable to Determine Size of Variable %s\n", vrname, EXIT_FAILURE);
  return atoi(value);
}

/* Parse Data Array */
void prsdta(void) {
  DEBUG("Parsing Array Data\n", 0)
  int i;
  dtype = DTARRY;
  expect('{');
  dlen = 0;
  do {
    skpspc();
    if (match('"')) { //Parse and Add String (including terminator)
      getstr(); for (i=0; i<=wrdlen; i++) dattmp[dlen++] = word[i];
    } else { //Parse and Add Literal
      prslit(); dattmp[dlen++] = litval;
    }
  } while (look(','));
  expect('}');
}

/* Parse Data String */
void prsdts(void) {
  dtype = DTSTR;
  getstr();
  strcpy(value, word);
  DEBUG("Parsed Data String '%s'\n", value)
}

/* Store variable data              *
 * Uses: value    - Data to store   *
 * Sets: datvar[] - Variable Data   *
 *       datlen[] - Data Length     */
void setdat(void) {
  int i;
  if (dtype == DTBYTE) {
    DEBUG("Setting variable data to '%d'\n", litval)
    dlen = 1;
    datvar[dsize++] = litval;
  }
  else if (dtype == DTARRY) {
	DEBUG("Setting variable data to array of length %d\n", dlen)
    for (i=0; i<dlen; i++)  datvar[dsize++] = dattmp[i];   
  }
  else {
    DEBUG("Setting variable data to '%s'\n", value)
    dlen = strlen(value);
    for (i=0; i<dlen; i++) datvar[dsize++] = value[i];   
  } 
  datlen[varcnt] = dlen;
  dattyp[varcnt] = dtype;
  DEBUG("Total data alllocated: %d bytes\n", dsize)
}

/* Parse and store variable data */
void prsdat(int m) {
  if ((m & MTCONST) == 0) ERROR("Initialization allowed only on variables declared CONST\n", 0, EXIT_FAILURE);
  DEBUG("Parsing variable data\n", 0)
  skpspc();
  if (islpre()) {dtype = DTBYTE; prslit(); } //Parse Data Literal
  else if (match('"')) prsdts();       //Parse Data String
  else if (match('{')) prsdta();       //Parse Data Array
  else expctd("numeric or string literal");
  if (alcvar || dtype == DTBYTE) setdat();   //Store Data Value
}

/* Add Variable to Variable table *
 * Uses: vrname - variable name   *
 *       value - variable size    */
void setvar(int m, int t) {
  DEBUG("Added variable '%s' ", vrname);
  strncpy(varnam[varcnt], vrname, VARLEN);
  varmod[varcnt] = m;
  vartyp[varcnt] = t;
  strncpy(varsiz[varcnt], value, 3);
  varstc[varcnt] = (t == VTSTRUCT) ? stcidx : -1;
  DETAIL("at index %d\n", varcnt);
}

/* Parse and Compile Variable Declaration *
 * Uses: word - variable name             */
void addvar(int m, int t) {
  strcpy(vrname, word); //Save Variable Name
  if (fndvar(vrname)) ERROR("Duplicate declaration of variable '%s\n", vrname, EXIT_FAILURE)
  if (t == VTVOID) ERROR("Illegal Variable Type\n", 0, EXIT_FAILURE)
  if (m & MTZP) {
    setlbl(vrname);
    sprintf(word, "$%hhX", zpaddr++);
    asmlin(EQUOP, word);
    strcpy(value, "*"); //Set Variable to Non Allocated  
  }
  else if (m & MTALS) {
    setlbl(vrname);
    skpspc();
    expect('=');
    skpspc();
    if (isnpre()) prsnum(0xFFFF); else prsvar(FALSE, FALSE);
    asmlin(EQUOP, word);
    strcpy(value, "*"); //Set Variable to Non Allocated  	
  }
  else {
    if (t == VTSTRUCT) {
      DEBUG("Setting variable size to %d\n", strct.size)
      sprintf(value, "%d", strct.size);
    } else if (match('[')) {
      CCMNT('[')
      skpchr();
      if (alcvar) {
        DEBUG("Parsing array size\n", 0)
        sprintf(value, "%d", prsnum(0xFF) + 1);
      }
      expect(']');
    }
    else value[0] = 0;
    if (!alcvar) strcpy(value, "*");  
  }  
  setvar(m, t);  //Add to Variable Table
  if (look('=')) prsdat(m); //Parse Variable Data
  varcnt++;   //Increment Variable Counter
}

/* Write Variable Definitions        *
 * Args: m = write CONST vars flag */
void vardef(int m) {
  int i, j;
  DEBUG("Writing Variable Table\n", 0)
  fprintf(logfil, "\n%-8s %s %s %s %s %s\n", "Variable", "Mod", "Type", "Size", "Struct", "Data");
  dlen = 0;
  for (i=0; i<varcnt; i++) {
    if ((varmod[i] & MTCONST) != m) { dlen += datlen[i]; continue; }
    fprintf(logfil, "%-8s %3d %4d %4s %6d %1d-%d\n", varnam[i], varmod[i], vartyp[i], varsiz[i], varstc[i], dattyp[i], datlen[i]);
    strcpy(lblasm, varnam[i]);
    DEBUG("Set Label to '%s'\n", lblasm)
    if (strcmp(varsiz[i], "*") == 0) continue;
    if (varmod[i] & MTALGN) {
      DEBUG("Aligning variable '%s'\n", varnam[i])
      asmlin(ALNOP, "256");
    }
    if (datlen[i]) {
      DEBUG("Building Data for Variable '%s'", varnam[i])
      DETAIL(" with length %d\n", datlen[i]);
      value[0] = 0;
      for (j=0; j<datlen[i]; j++) {
        if (strlen(value)) strcat(value,",");
        sprintf(word, "$%hhX", datvar[dlen++]);
        strcat(value, word);
        if (strlen(value)>64) {asmlin(BYTEOP,value); value[0]=0;}
      }
      if (dattyp[i] == DTSTR) {
        if (strlen(value)) strcat(value,",");
        strcat(value, "$00");
      }
      DEBUG("Allocating Data for Variable '%s'\n", varnam[i])
      asmlin(BYTEOP, value);
    }
    else if (strlen(varsiz[i]) > 0) {
      DEBUG("Allocating array '%s'\n", varnam[i])
      asmlin(STROP, varsiz[i]);
    }
    else {
      DEBUG("Allocating variable '%s'\n", varnam[i])
      asmlin(BYTEOP, "0");
    }
  }
  vrwrtn = TRUE;
}

/* Write Variable Table */
void vartbl(void) {
  LCMNT("Variables declared CONST")
  vardef(MTCONST); //Write CONST Definitions
  //Emit Segment Mnemonic for RAM Variables here
  LCMNT("Writable Variables")
  if (rambas) {
    asmlin(USEGOP,"RAMVARS"); //Create Uninitialized Segment
    sprintf(word, "$%X", rambas); 
    asmlin(ORGOP, word);      //Set Origin to RAM Base Address
  }
  vardef(0); //Write All Other Variables
}

/* Parse and Compile Struct Declaration */
void addstc(void) {
  if (!fndstc(word)) ERROR("Undefined Struct '%s\n", word,EXIT_FAILURE)
  strct = strcts[stcidx]; //Retrieve Structure
  getwrd(); //Get Variable Name
  addvar(MTNONE, VTSTRUCT);
  expect(';');
}

/* Parse Struct Definition  *
 * Uses: word - Struct Name */
void defstc(void) {
  DEBUG("Parsing struct definition\n", 0)
  if (fndstc(word)) ERROR("Duplicate Declaration of Struct '%s\n", word,EXIT_FAILURE)
  int prnidx = stcidx;
  strncpy(strct.name, word, STCLEN);
  DEBUG("Set struct name to '%s'\n", word);
  strct.size = 0; //Initialize Struct Length
  do {
	getwrd(); //Get Member Name
    if (wordis("STRUCT")) {
      getwrd(); //Get Structure Name
      if (!fndstc(word)) ERROR("Structure '%s' Not Defined\n", word, EXIT_FAILURE)
	  membr.stype = STRUCTURE;
      membr.size = strcts[stcidx].size;
      membr.symidx = stcidx;
	  getwrd(); //Get Member Name
    } else {
      if (wordis("CHAR")) getwrd(); //Skip Optional Type Declaration
	  membr.stype = VARIABLE;
      membr.size = 1;
      membr.symidx = -1;
    }
    if (fndmbr(stccnt, word)) ERROR("Duplicate Declaration of Struct Member '%s\n", word,EXIT_FAILURE)
    DEBUG("Parsing member %s\n", word)
    strncpy(membr.name, word, STMLEN); //Set Member Name
    membr.strcti = prnidx;             //Set Parent Struct Index
    membr.offset = strct.size;         //Set Offset into Struct
    if (membr.stype == VARIABLE) {
    DEBUG("Checking for array definition\n", 0)
      if (match('[')) {
        CCMNT('[');
        skpchr();
        membr.stype = ARRAY;
        DEBUG("Parsing array size\n", 0)
        membr.size = prsnum(0xFF) + 1;
        expect(']');
      }
    }
    DEBUG("Set member type to %d", membr.stype) DETAIL(" and size to %d\n", membr.size);
    DEBUG("Adding member at index %d\n", mbrcnt);
    membrs[mbrcnt++] = membr;
    strct.size += membr.size;
    expect(';');
  } while (!look('}'));
  expect(';');
  if (strct.size > 256) ERROR("Structure Size %d Exceeds Limit of 256 bytes.\n", strct.size, EXIT_FAILURE);  
  DEBUG("Adding struct with size %d", strct.size) DETAIL("at index %d\n", stccnt);
  strcts[stccnt++] = strct;
}

/* Print Struc Tables to Log File */
void logstc(void) {
  fprintf(logfil, "\n%-8s %5s\n", "Struct", "Size");
  for (stcidx=0; stcidx<stccnt; stcidx++) {
    fprintf(logfil, "%-8s %5d\n", strcts[stcidx].name, strcts[stcidx].size);
  }
  fprintf(logfil, "\n%-8s %-8s", "Struct", "Member");
  fprintf(logfil, " %5s %5s %6s %5s\n", "Type", "Index", "Offset", "Size");
  for (mbridx=0; mbridx<mbrcnt; mbridx++) {
    membr = membrs[mbridx];
    fprintf(logfil, "%-8s %-8s", strcts[membr.strcti].name, membr.name);
    fprintf(logfil, " %5d %5d %6d %5d\n", membr.stype, membr.symidx, membr.offset, membr.size);
  }

}
