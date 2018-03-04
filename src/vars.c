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
int fndvar(char *name) 
{
  DEBUG("Looking up variable '%s'\n", word);
  for (varidx=0; varidx<varcnt; varidx++) {
    if (strcmp(varnam[varidx], name) == 0) 
      return TRUE;
  }
  return FALSE;
}

/* Check for variable                       *
 * Generates error if variable is undefined *
 * Args: alwreg - allow register name       *
 *       name - variable name               */
void chksym(int alwreg, char *name) 
{
  if (strlen(name) == 1 && strchr("AXY", name[0])) {
    if (alwreg && valtyp != ARRAY) {
      valtyp = REGISTER;
  	  return;
    }
    ERROR("Illegal reference to register %s\n", name, EXIT_FAILURE);
  }
  if (!fndvar(name))
    ERROR("Undeclared variable '%s' encountered\n", name, EXIT_FAILURE);
}

/* Parse next word as variable or function name *
 * Args: alwreg - Allow Register Names          *
 * Sets: value - Identifier Name                *
 *       valtyp - Identifier Type               */
void prsvar(int alwreg) 
{
  getwrd();
  valtyp = gettyp();
  if (valtyp != FUNCTION) chksym(alwreg, word);
  strcpy(value, word);
  DEBUG("Parsed variable '%s'\n", value);
}

/* Require and Parse Variable Name                         *
 * Parameters: alwary - Allow Array Reference *
 * Sets: vrname - operand for LDA/STA/LDY/STY */
void reqvar(int alwary)
{
  prsvar(FALSE);
  if (!alwary)
    if (valtyp != VARIABLE) 
      expctd("Variable");
} 

/* Parse Data Array */
void prsdta()
{
  dtype = DTARRY;
  expect('{');
  dlen = 0;
  while (TRUE) {
    prscon();
    dattmp[dlen++] = cnstnt;
    if (!look(',')) 
      break;  
  }
  expect('}');
}

/* Parse Data String */
void prsdts()
{
  dtype = DTSTR;
  getstr();
  strcpy(value, word);
  DEBUG("Parsed Data String '%s'\n", value);
}

/* Store variable data              *
 * Uses: value    - Data to store   *
 * Sets: datvar[] - Variable Data   *
 *       datlen[] - Data Length     */
void setdat()
{
  int i;
  if (dtype == DTBYTE) {
    DEBUG("Setting variable data to '%d'\n", cnstnt);
    dlen = 1;
    datvar[dsize++] = cnstnt;
  }
  else if (dtype == DTARRY) {
    DEBUG("Setting variable data to array of length %d\n", dlen);
    for (i=0; i<dlen; i++) 
      datvar[dsize++] = dattmp[i];   
  }
  else {
    DEBUG("Setting variable data to '%s'\n", value);
    dlen = strlen(value);
    for (i=0; i<dlen; i++) 
      datvar[dsize++] = value[i];   
  } 
  datlen[varcnt] = dlen;
  dattyp[varcnt] = dtype;
  DEBUG("Total data alllocated: %d bytes\n", dsize);  
}

/* Parse and store variable data */
void prsdat()
{
  DEBUG("Checking for variable data\n", 0);
  if (!look('=')) {
    datlen[varcnt] = 0;
    return;
  }
  skpspc();
  if (iscpre()) {
    dtype = DTBYTE;
    prscon(); //Parse Data Constant
  }
  else if (match('"'))
    prsdts();       //Parse Data String
  else if (match('{'))
    prsdta();       //Parse Data Array
  else
    expctd("numeric or string constant");
  setdat();   //Store Data Value
}

/* Add Variable to Variable table *
 * Uses: word - variable name     *
 *       value - variable size    */
void setvar(int m, int t) 
{
  DEBUG("Added variable '%s' ", word);
  strncpy(varnam[varcnt], vrname, VARLEN);
  varmod[varcnt] = m;
  vartyp[varcnt] = t;
  strncpy(varsiz[varcnt], value, 3);
  DETAIL("at index %d\n", varcnt);
}

/* Parse and Compile Variable Declaration *
 * Uses: word - variable name     */
void addvar(int m, int t) 
{
  strcpy(vrname, word); //Save Variable Name
  if (fndvar(vrname))
    ERROR("Duplicate declaration of variable '%s\n", word,EXIT_FAILURE);
  if (t == VTVOID)
    ERROR("Illegal Variable Type\n", 0, EXIT_FAILURE);
  if (m == MTZP) {
    setlbl(vrname);
    sprintf(word, "$%hhX", zpaddr++);
    asmlin(EQUOP, word);
    strcpy(value, "*"); //Set Variable Type to Zero Page  
  }
  else {
    DEBUG("Checking for array definition\n", 0);
    value[0] = 0;
    if (match('[')) {
      skpchr();
      if (alcvar) {
        DEBUG("Parsing array size\n", 0);
        sprintf(value, "%d", prsnum(0xFF) + 1);
      }
      expect(']');
    }
    if (!alcvar) strcpy(value, "*");  
    setvar(m, t);  //Add to Variable Table
  }  
  if (m != MTZP)
    prsdat();   //Parse Variable Data
  varcnt++;   //Increment Variable Counter
}


/* Write Variable Table */
void vartbl()
{
  int i, j;
  DEBUG("Writing Variable Table", 0);
  fprintf(logfil, "\n%-31s %s %s %s\n", "Variable", "Type", "Size", "Data");
  dlen = 0;
  for (i=0; i<varcnt; i++) {
    fprintf(logfil, "%-31s %4d %4s %1d-%d\n", varnam[i], vartyp[i], varsiz[i], dattyp[i], datlen[i]);
    strcpy(lblasm, varnam[i]);
    DEBUG("Set Label to '%s'\n", lblasm);
    if (strcmp(varsiz[i], "*") == 0)
      continue;
    if (varmod[i] == MTALGN) {
      DEBUG("Alligning variable '%s'\n", varnam[i]);
      asmlin(ALNOP, "256");
    }
    if (datlen[i]) {
      DEBUG("Building Data for Variable '%s'\n", varnam[i]);
      value[0] = 0;
      for (j=0; j<datlen[i]; j++) {
        if (j) strcat(value,",");
        sprintf(word, "$%hhX", datvar[dlen++]);
        strcat(value, word);  
      }
      if (dattyp[i] == DTSTR) strcat(value, ",$00");
      DEBUG("Allocating Data for Variable '%s'\n", varnam[i]);
      asmlin(BYTEOP, value);
  }
    else if (strlen(varsiz[i]) > 0) {
      DEBUG("Allocating array '%s'\n", varnam[i]);
      asmlin(STROP, varsiz[i]);
    }
    else {
      DEBUG("Allocating variable '%s'\n", varnam[i]);
      asmlin(BYTEOP, "0");
    }
  }
  vrwrtn = TRUE;
}