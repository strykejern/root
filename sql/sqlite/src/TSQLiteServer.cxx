// @(#)root/sqlite:$Id$
// Author: o.freyermuth <o.f@cern.ch>, 01/06/2013

/*************************************************************************
 * Copyright (C) 1995-2013, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TSQLiteServer.h"
#include "TSQLiteResult.h"
#include "TSQLiteStatement.h"
#include "TUrl.h"

ClassImp(TSQLiteServer)

//______________________________________________________________________________
TSQLiteServer::TSQLiteServer(const char *db, const char* /*uid*/, const char* /*pw*/)
{
   // Open a connection to an SQLite DB server. The db arguments should be
   // of the form "sqlite://<database>", e.g.:
   // "sqlite://test.sqlite" or "sqlite://:memory:" for a temporary database
   // in memory.
   // Note that for SQLite versions >= 3.7.7 the full string behind
   // "sqlite://" is handed to sqlite3_open_v2() with SQLITE_OPEN_URI activated,
   // so all URI accepted by it can be used.

   fSQLite = NULL;
   fSrvInfo = "SQLite ";
   fSrvInfo += sqlite3_libversion();

   if (strncmp(db, "sqlite://", 9)) {
      TString givenProtocol(db, 9); // this TString-constructor allocs len+1 and does \0 termination already.
      Error("TSQLiteServer", "protocol in db argument should be sqlite it is %s",
            givenProtocol.Data());
      MakeZombie();
      return;
   }

   const char *dbase = db + 9;

#ifndef SQLITE_OPEN_URI
#define SQLITE_OPEN_URI 0x00000000
#endif
#if SQLITE_VERSION_NUMBER >= 3005000
   Int_t error = sqlite3_open_v2(dbase, &fSQLite, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, NULL);
#else
   Int_t error = sqlite3_open(dbase, &fSQLite);
#endif

   if (error == 0) {
      // Set members of the abstract interface
      fType = "SQLite";
      fHost = "";
      fDB = dbase;
      // fPort != -1 means we are 'connected'
      fPort = 0;
   } else {
      Error("TSQLiteServer", "opening of %s failed with error: %d %s", dbase, sqlite3_errcode(fSQLite), sqlite3_errmsg(fSQLite));
      sqlite3_close(fSQLite);
      MakeZombie();
   }

}

//______________________________________________________________________________
TSQLiteServer::~TSQLiteServer()
{
   // Close SQLite DB.

   if (IsConnected()) {
      sqlite3_close(fSQLite);
   }
}

//______________________________________________________________________________
void TSQLiteServer::Close(Option_t *)
{
   // Close connection to SQLite DB.

   if (!fSQLite) {
      return;
   }

   sqlite3_close(fSQLite);
}

//______________________________________________________________________________
Bool_t TSQLiteServer::StartTransaction()
{
   // submit "START TRANSACTION" query to database
   // return kTRUE, if successful

   return Exec("BEGIN TRANSACTION");
}

//______________________________________________________________________________
TSQLResult *TSQLiteServer::Query(const char *sql)
{
   // Execute SQL command. Result object must be deleted by the user.
   // Returns a pointer to a TSQLResult object if successful, 0 otherwise.
   // The result object must be deleted by the user.

   if (!IsConnected()) {
      Error("Query", "not connected");
      return 0;
   }

   sqlite3_stmt *preparedStmt = NULL;

   // -1 as we read until we encounter a \0.
   // NULL because we do not check which char was read last.
#if SQLITE_VERSION_NUMBER >= 3005000
   int retVal = sqlite3_prepare_v2(fSQLite, sql, -1, &preparedStmt, NULL);
#else
   int retVal = sqlite3_prepare(fSQLite, sql, -1, &preparedStmt, NULL);
#endif
   if (retVal != SQLITE_OK) {
      Error("Query", "SQL Error: %d %s", retVal, sqlite3_errmsg(fSQLite));
      return 0;
   }

   return new TSQLiteResult(preparedStmt);
}

//______________________________________________________________________________
Bool_t TSQLiteServer::Exec(const char *sql)
{
   // Execute SQL command which does not produce any result sets.
   // Returns kTRUE if successful.

   char *sqlite_err_msg;
   int ret = sqlite3_exec(fSQLite, sql, NULL, NULL, &sqlite_err_msg);
   if (ret != SQLITE_OK) {
      Error("Exec", "SQL Error: %d %s", ret, sqlite_err_msg);
      sqlite3_free(sqlite_err_msg);
      return kFALSE;
   }
   return kTRUE;
}


//______________________________________________________________________________
Int_t TSQLiteServer::SelectDataBase(const char* /*dbname*/)
{
   // Select a database. Always returns non-zero for SQLite,
   // as only one DB exists per file.

   Error("SelectDataBase", "SelectDataBase command makes no sense for SQLite!");
   return -1;
}

//______________________________________________________________________________
TSQLResult *TSQLiteServer::GetDataBases(const char* /*wild*/)
{
   // List all available databases. Always returns 0 for SQLite,
   // as only one DB exists per file.

   Error("GetDataBases", "GetDataBases command makes no sense for SQLite!");
   return 0;
}

//______________________________________________________________________________
TSQLResult *TSQLiteServer::GetTables(const char* /*dbname*/, const char *wild)
{
   // List all tables in the specified database. Wild is for wildcarding
   // "t%" list all tables starting with "t".
   // Returns a pointer to a TSQLResult object if successful, 0 otherwise.
   // The result object must be deleted by the user.

   if (!IsConnected()) {
      Error("GetTables", "not connected");
      return 0;
   }

   TString sql = "SELECT name FROM sqlite_master where type='table'";
   if (wild)
      sql += Form(" AND name LIKE '%s'", wild);

   return Query(sql);
}

//______________________________________________________________________________
TSQLResult *TSQLiteServer::GetColumns(const char* /*dbname*/, const char* /*table*/,
      const char* /*wild*/)
{
   // List all columns in specified table (database argument is ignored).
   // Wild is for wildcarding "t%" list all columns starting with "t".
   // Returns a pointer to a TSQLResult object if successful, 0 otherwise.
   // The result object must be deleted by the user.
   // For SQLite, this always fails, as the column names are not queryable!

   if (!IsConnected()) {
      Error("GetColumns", "not connected");
      return 0;
   }

   Error("GetColumns", "Not implementable for SQLite as a query, use GetFieldNames() after SELECT instead!");

   // "PRAGMA table_info (%s)", table only returns an ugly string, and
   // this can not be used in a SELECT

   return NULL;
}

//______________________________________________________________________________
Int_t TSQLiteServer::CreateDataBase(const char* /*dbname*/)
{
   // Create a database. Always returns non-zero for SQLite,
   // as it has only one DB per file.

   Error("CreateDataBase", "CreateDataBase command makes no sense for SQLite!");
   return -1;
}

//______________________________________________________________________________
Int_t TSQLiteServer::DropDataBase(const char* /*dbname*/)
{
   // Drop (i.e. delete) a database. Always returns non-zero for SQLite,
   // as it has only one DB per file.

   Error("DropDataBase", "DropDataBase command makes no sense for SQLite!");
   return -1;
}

//______________________________________________________________________________
Int_t TSQLiteServer::Reload()
{
   // Reload permission tables. Returns 0 if successful, non-zero
   // otherwise. User must have reload permissions.

   if (!IsConnected()) {
      Error("Reload", "not connected");
      return -1;
   }

   Error("Reload", "not implemented");
   return 0;
}

//______________________________________________________________________________
Int_t TSQLiteServer::Shutdown()
{
   // Shutdown the database server. Returns 0 if successful, non-zero
   // otherwise. Makes no sense for SQLite, always returns -1.

   if (!IsConnected()) {
      Error("Shutdown", "not connected");
      return -1;
   }

   Error("Shutdown", "not implemented");
   return -1;
}

//______________________________________________________________________________
Bool_t TSQLiteServer::HasStatement() const
{
   // We assume prepared statements work for all SQLite-versions.
   // As we actually use the recommended sqlite3_prepare(),
   // or, if possible, sqlite3_prepare_v2(),
   // this already introduces the "compile time check".

   return kTRUE;
}

//______________________________________________________________________________
TSQLStatement* TSQLiteServer::Statement(const char *sql, Int_t)
{
   // Produce TSQLiteStatement.

   if (!sql || !*sql) {
      SetError(-1, "no query string specified", "Statement");
      return 0;
   }

   sqlite3_stmt *preparedStmt = NULL;

   // -1 as we read until we encounter a \0.
   // NULL because we do not check which char was read last.
#if SQLITE_VERSION_NUMBER >= 3005000
   int retVal = sqlite3_prepare_v2(fSQLite, sql, -1, &preparedStmt, NULL);
#else
   int retVal = sqlite3_prepare(fSQLite, sql, -1, &preparedStmt, NULL);
#endif
   if (retVal != SQLITE_OK) {
      Error("Statement", "SQL Error: %d %s", retVal, sqlite3_errmsg(fSQLite));
      return 0;
   }

   SQLite3_Stmt_t *stmt = new SQLite3_Stmt_t;
   stmt->fConn = fSQLite;
   stmt->fRes  = preparedStmt;

   return new TSQLiteStatement(stmt);
}

//______________________________________________________________________________
const char *TSQLiteServer::ServerInfo()
{
   // Return server info, must be deleted by user.

   if (!IsConnected()) {
      Error("ServerInfo", "not connected");
      return 0;
   }

   return fSrvInfo.Data();
}

