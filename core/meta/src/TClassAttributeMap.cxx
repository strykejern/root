// @(#)root/meta:$Id:$
// Author: Bianca-Cristina Cristescu   03/07/13

/*************************************************************************
 * Copyright (C) 1995-2013, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  The ROOT oject has a list of properties which are stored and       //
//  retrieved using TClassAttributeMap.                                 //
//  TClassAttributeMap maps the property keys of the object to their		//
//  values.															    //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TClassAttributeMap.h"
#include "THashTable.h"
#include "TNamed.h"
#include "TParameter.h"


ClassImp(TClassAttributeMap)

//_____________________________________________________________________________
TClassAttributeMap::TClassAttributeMap()
{
   //Default constructor.
   fIntProperty.SetOwner(kTRUE);
   fStringProperty.SetOwner(kTRUE);
}

//_____________________________________________________________________________
TClassAttributeMap::~TClassAttributeMap()
{
   //Default destructor.
}

//_____________________________________________________________________________
void TClassAttributeMap::AddProperty(const char* key, Int_t value)
{
   //Add a property with a Int value to the TClassAttributeMap.
   //Parameters: key and Int value of the property.

   //Add the property pair name - Int value to the hash table.
   fIntProperty.Add(new TParameter<int>(key, value));
}

//_____________________________________________________________________________
void TClassAttributeMap::AddProperty(const char* key, const char* value)
{
   //Add a property with a String value to the TClassAttributeMap.
   //Parameters: key and char* value of the property.

   //Add the property pair name - Int value to the hash table.
   fStringProperty.Add(new TNamed(key, value));
}

//_____________________________________________________________________________
Bool_t TClassAttributeMap::HasKey(const char* key) const
{

   //Check whether the class has a property using the key.

   if (fStringProperty.FindObject(key))
      return true;
   if (fIntProperty.FindObject(key))
      return true;
   return false;
}

//_____________________________________________________________________________
Int_t TClassAttributeMap::GetPropertyAsInt(const char* key) const
{
   //Access the value of a Int property using the key.

   //Copy object into found to avoid calling the function two times.
   TObject* found = fIntProperty.FindObject(key);
   if (found)
      return ((TParameter<int>*)found)->GetVal();
   else
      //Show an error message if the key is not found.
      Error("GetPropertyAsInt"
      , "Could not find property with Int value for this key: %s", key);
   return -1;
}

//_____________________________________________________________________________
const char* TClassAttributeMap::GetPropertyAsString(const char* key) const
{
   //Access the value of a String property using the key.

   //Copy object into found to avoid calling the function two times.
   TObject* found = fStringProperty.FindObject(key);
   if(found)
      return found->GetTitle();
   else
      //Show an error message if the key is not found.
      Error("GetPropertyAsString"
      , "Could not find property with String value for this key: %s", key);
   return 0;
}

//_____________________________________________________________________________
Int_t TClassAttributeMap::GetPropertySize() const
{
   //Return the number of properties of the class as Int_t.

   return fIntProperty.GetSize() + fStringProperty.GetSize();
}

//_____________________________________________________________________________
Int_t TClassAttributeMap::RemovePropertyInt(const char* key)
{
   //Remove a Int property from the attribute map.
   //Returns the Int property removed or -1 if the property does not exist.

   TParameter<int> *property = (TParameter<int>*)fIntProperty.FindObject(key);
   if (property) {
     fIntProperty.Remove(property);
     return property->GetVal();
   }
   return -1;
}

//_____________________________________________________________________________
TString TClassAttributeMap::RemovePropertyString(const char* key)
{
   //Remove a String property from the attribute map specified by the key.
   //Returns the TString property removed or 0 if the property does not exist.

   TObject *property = fStringProperty.FindObject(key);
   if (property) {
      fStringProperty.Remove(property);
      return property->GetTitle();
   }
   return TString(0);
}

Bool_t TClassAttributeMap::RemoveProperty(const char* key)
{
   //Remove a property from the attribute map specified by the key.
   //Returns true if property exists and was removed, false if property
   //does not exist.

   if (TObject *property = fStringProperty.FindObject(key)) {
      fStringProperty.Remove(property);
      return true;
   }
   return false;
}

//_____________________________________________________________________________
void TClassAttributeMap::Clear(Option_t* /*option = ""*/)
{
   //Deletes all the properties of the class.

   fIntProperty.Delete();
   fStringProperty.Delete();
}
