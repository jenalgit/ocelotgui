/* Copyright (c) 2014-2016 by Ocelot Computer Services Inc. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*
  The routines that start with "hparse_*" are a predictive recursive-descent
  recognizer for MySQL, generally assuming LL(1) grammar but allowing for a few quirks.
  (A recognizer does all the recursive-descent parser stuff except that it generates no tree.)
  Generally recursive-descent parsers or recognizers are reputed to be good
  because they're simple and can produce good -- often predictive -- error messages,
  but bad because they're huge and slow, and that's certainly the case here.
  The intent is to make highlight and hover look good.
  If any comparison fails, the error message will say:
  what we expected, token number + offset + value for the token where comparison failed.
  Allowed syntaxes are: MySQL_5.7, MariaDB 10.2, semi-SQLite, or an Ocelot client statement.
*/

/*
  Todo: Actually identifier length maximum isn't always 64.
  See http://dev.mysql.com/doc/refman/5.7/en/identifiers.html
*/
#define MYSQL_MAX_IDENTIFIER_LENGTH 64

/*
  Currently allow_flags only tries to detect parenthesized expressions
  with multiple operands inside (which are only allowed for comp-ops).
  It could be expanded to check for whether subqueries are allowed
  (currently we depend on hparse_subquery_is_allowed), and even
  data type e.g. no string literal after << operator.
*/
#define ALLOW_FLAG_IS_MULTI 1
#define ALLOW_FLAG_IS_ANY (1)

void MainWindow::hparse_f_nexttoken()
{
  if (hparse_errno > 0) return;
  for (;;)
  {
    ++hparse_i;
    hparse_token_type= main_token_types[hparse_i];
    if ((hparse_token_type == TOKEN_TYPE_LITERAL_WITH_DOUBLE_QUOTE)
     && (hparse_sql_mode_ansi_quotes == true))
    {
      hparse_token_type= main_token_types[hparse_i]= TOKEN_TYPE_IDENTIFIER_WITH_DOUBLE_QUOTE;
    }
    if ((hparse_token_type != TOKEN_TYPE_COMMENT_WITH_SLASH)
     && (hparse_token_type != TOKEN_TYPE_COMMENT_WITH_OCTOTHORPE)
     && (hparse_token_type != TOKEN_TYPE_COMMENT_WITH_MINUS))
      break;
  }
  hparse_prev_token= hparse_token;
  hparse_token= hparse_text_copy.mid(main_token_offsets[hparse_i], main_token_lengths[hparse_i]);
}

/*
  Lookahead.
  Call this if you want to know what the next symbol is, but don't want to get it.
  This is used in only five places, to see whether ":" follows (which could indicate a label),
  and to see whether next is "." and next_next is "*" (as in a select-list),
  and to see whether NOT is the beginning of NOT LIKE,
  and to see whether the word following GRANT ROLE is TO,
  and to see whether the word following DATE|TIME|TIMESTAMP is a literal.
*/
void MainWindow::hparse_f_next_nexttoken()
{
  hparse_next_token= hparse_next_next_token= "";
  hparse_next_next_next_token= "";
  hparse_next_next_next_next_token= "";
  int saved_hparse_i= hparse_i;
  int saved_hparse_token_type= hparse_token_type;
  QString saved_hparse_token= hparse_token;
  if (main_token_lengths[hparse_i] != 0)
  {
    hparse_f_nexttoken();
    hparse_next_token= hparse_token;
    hparse_next_token_type= hparse_token_type;
    if (main_token_lengths[hparse_i] != 0)
    {
      hparse_f_nexttoken();
      hparse_next_next_token= hparse_token;
      hparse_next_next_token_type= hparse_token_type;
    }
    if (main_token_lengths[hparse_i] != 0)
    {
      hparse_f_nexttoken();
      hparse_next_next_next_token= hparse_token;
      hparse_next_next_next_token_type= hparse_token_type;
    }
    if (main_token_lengths[hparse_i] != 0)
    {
      hparse_f_nexttoken();
      hparse_next_next_next_next_token= hparse_token;
      hparse_next_next_next_next_token_type= hparse_token_type;
    }
  }
  hparse_i= saved_hparse_i;
  hparse_token_type= saved_hparse_token_type;
  hparse_token= saved_hparse_token;
}

void MainWindow::hparse_f_error()
{
  if (hparse_errno > 0) return;
  assert(hparse_i >= 0);
  assert(hparse_i < (int)main_token_max_count);
  main_token_flags[hparse_i] |= TOKEN_FLAG_IS_ERROR;
  QString q_errormsg= "The latest token is: ";
  if (hparse_token.length() > 40)
  {
    q_errormsg.append(hparse_token.left(40));
    q_errormsg.append("...");
  }
  else q_errormsg.append(hparse_token);
  q_errormsg.append("  (token #");
  q_errormsg.append(QString::number(hparse_i + 1));
  q_errormsg.append(", offset ");
  q_errormsg.append(QString::number(main_token_offsets[hparse_i] + 1));
  q_errormsg.append(") ");
  q_errormsg.append(". The list of expected tokens is: ");
  q_errormsg.append(hparse_expected);
  while ((unsigned) q_errormsg.toUtf8().length() >= (unsigned int) sizeof(hparse_errmsg) - 1)
    q_errormsg= q_errormsg.left(q_errormsg.length() - 1);
  strcpy(hparse_errmsg, q_errormsg.toUtf8());
  hparse_errno= 10400;
}

/*
  Merely saying "if (hparse_token == 'x') ..." till we saw delimiter usually is not =.
*/
bool MainWindow::hparse_f_is_equal(QString hparse_token_copy, QString token)
{
  if (hparse_token_copy == hparse_delimiter_str) return false;
  if (hparse_token_copy == token) return true;
  return false;
}

/*
  Tarantool only -- after WITH, and within CREATE TRIGGER,
  only certain verbs are legal.
*/
bool MainWindow::hparse_f_is_special_verb(int outer_verb)
{
  QString s= hparse_token.toUpper();
  if ((s== "DELETE") || (s == "INSERT") || (s == "REPLACE")
   || (s== "SELECT") || (s == "UPDATE") || (s == "VALUES"))
    return true;
  if ((outer_verb == TOKEN_KEYWORD_TRIGGER) && (s == "WITH")) return true;
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DELETE, "DELETE");
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_INSERT, "INSERT");
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPLACE, "REPLACE");
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SELECT, "SELECT");
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_UPDATE, "UPDATE");
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_VALUES, "VALUES");
  if (outer_verb == TOKEN_KEYWORD_TRIGGER)
    hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_WITH, "WITH");
  hparse_f_error();
  return false;
}

/*
  accept means: if current == expected then clear list of what was expected, get next, and return 1,
                else add to list of what was expected, and return 0
*/
int MainWindow::hparse_f_accept(unsigned short int flag_version, unsigned char reftype, int proposed_type, QString token)
{
  if (hparse_errno > 0) return 0;
  if ((hparse_dbms_mask & flag_version) == 0) return 0;
  bool equality= false;
  if (token == "[eof]")
  {
    if (hparse_token.length() == 0)
    {
      equality= true;
    }
  }
  else if ((hparse_token == hparse_delimiter_str) && (hparse_delimiter_str != ";"))
  {
    if ((hparse_token == token) && (proposed_type == TOKEN_TYPE_DELIMITER)) equality= true;
    else equality= false;
  }
  else if (hparse_text_copy.mid(main_token_offsets[hparse_i], 2).toUpper() == "\\G")
  {
    /* \G and \g can act somewhat like delimiters. No change to hparse_expected list. */
    if (proposed_type == TOKEN_TYPE_DELIMITER)
    {
      //main_token_types[hparse_i]= proposed_type;
      //main_token_types[hparse_i + 1]= proposed_type;
      hparse_expected= "";
      hparse_f_nexttoken();
      hparse_i_of_last_accepted= hparse_i;
      hparse_f_nexttoken();
      ++hparse_count_of_accepts;
      return 1;
    }
    return 0;
  }
  else if (token == "[identifier]")
  {
    /* todo: stop checking if it's "[identifier]" when reftype is always passed. */
    if ((hparse_dbms_mask & FLAG_VERSION_LUA) != 0)
    {
      if ((main_token_flags[hparse_i] & TOKEN_FLAG_IS_MAYBE_LUA) != 0)
      {
        main_token_flags[hparse_i] |= TOKEN_FLAG_IS_RESERVED;
      }
      else
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        if ((hparse_token_type >= TOKEN_TYPE_OTHER)
         || (hparse_token_type == TOKEN_TYPE_IDENTIFIER))
          equality= true;
      }
    }
    else
    {
      if (hparse_token_type == TOKEN_TYPE_IDENTIFIER_WITH_BACKTICK)
      {
        if ((hparse_token.size() == 1) || (hparse_token.right(1) != "`"))
        {
          /* Starts with ` but doesn't end with ` so identifier required but not there yet. */
          /* TODO: hparse_expected= ""; equality stays false; fall though */
          main_token_reftypes[hparse_i]= reftype;
          hparse_expected= hparse_f_token_to_appendee(token, reftype);
          return 0;
        }
      }
      if (hparse_token_type == TOKEN_TYPE_IDENTIFIER_WITH_DOUBLE_QUOTE)
      {
        if ((hparse_token.size() == 1) || (hparse_token.right(1) != "\""))
        {
         /* Starts with " but doesn't end with " so identifier required but not there yet. */
            /* TODO: hparse_expected= ""; equality stays false; fall though */
          main_token_reftypes[hparse_i]= reftype;
          hparse_expected= hparse_f_token_to_appendee(token, reftype);
          return 0;
        }
      }
      if ((hparse_token_type == TOKEN_TYPE_IDENTIFIER_WITH_BACKTICK)
       || (hparse_token_type == TOKEN_TYPE_IDENTIFIER_WITH_DOUBLE_QUOTE)
       || (hparse_token_type == TOKEN_TYPE_IDENTIFIER_WITH_AT)
       || ((hparse_token_type >= TOKEN_TYPE_OTHER)
        && ((main_token_flags[hparse_i] & TOKEN_FLAG_IS_RESERVED) == 0)))
      {
        equality= true;
      }
    }
  }
  else if (token == "[literal]")
  {
    if (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_SINGLE_QUOTE)
    {
      if ((hparse_token.size() == 1) || (hparse_token.right(1) != "'"))
      {
        /* Starts with ' but doesn't end with ' so literal required but not there yet. */
        hparse_expected= token;
        return 0;
      }
    }
    if (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_DOUBLE_QUOTE)
    {
      if ((hparse_token.size() == 1) || (hparse_token.right(1) != "\""))
      {
        /* Starts with ' but doesn't end with " so literal required but not there yet. */
        hparse_expected= token;
        return 0;
      }
    }
    if (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_BRACKET)
    {
      if (hparse_token.right(2) != "]]")
      {
        /* Starts with [[ but doesn't end with ]] so literal required but not there yet. */
        hparse_expected= token;
        return 0;
      }
    }
    if ((hparse_token_type == TOKEN_TYPE_LITERAL_WITH_SINGLE_QUOTE)
     || (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_DOUBLE_QUOTE)
     || (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_DIGIT)
     || (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_BRACKET)
     /* literal_with_brace == literal */
     || (hparse_token_type == TOKEN_TYPE_LITERAL_WITH_BRACE)) /* obsolete? */
    {
      equality= true;
    }
  }
  else if (token == "[introducer]")
  {
    if ((hparse_token_type >= TOKEN_KEYWORD__ARMSCII8)
     && (hparse_token_type <= TOKEN_KEYWORD__UTF8MB4))
    {
      equality= true;
    }
  }
  else if (token == "[reserved function]")
  {
    if (((main_token_flags[hparse_i] & TOKEN_FLAG_IS_RESERVED) != 0)
     && ((main_token_flags[hparse_i] & TOKEN_FLAG_IS_FUNCTION) != 0))
      equality= true;
  }
#ifdef DBMS_TARANTOOL
  else if (token == "[field identifier]")
  {
    int base_size= strlen(TARANTOOL_FIELD_NAME_BASE);
    bool ok= false;
    int field_integer= 0;
    int field_integer_length= hparse_token.length() - (base_size + 1);
    if (field_integer_length > 0) field_integer= hparse_token.right(field_integer_length).toInt(&ok);
    if ((hparse_token.left(base_size) == TARANTOOL_FIELD_NAME_BASE)
     && (hparse_token.mid(base_size, 1) == "_")
     && (field_integer > 0)
     && (ok == true)
     && (hparse_token.length() < TARANTOOL_MAX_FIELD_NAME_LENGTH))
    {
      equality= true;
    }
  }
#endif
  else
  {
    if ((hparse_dbms_mask & FLAG_VERSION_LUA) != 0)
    {
      if (QString::compare(hparse_token, token, Qt::CaseSensitive) == 0)
      {
        equality= true;
      }
    }
    else if (QString::compare(hparse_token, token, Qt::CaseInsensitive) == 0)
    {
      equality= true;
    }
  }

  if (equality == true)
  {
    /*
      Change the token type now that we're sure what it is.
      But for keyword: if it's already more specific, leave it.
      TODO: that exception no longer works because we moved TOKEN_TYPE_KEYWORD to the end
      But for literal: if it's already more specific, leave it
    */
    if ((proposed_type == TOKEN_TYPE_KEYWORD)
     && (main_token_types[hparse_i] >= TOKEN_KEYWORDS_START)) {;}
    else if ((proposed_type == TOKEN_TYPE_LITERAL)
     && (main_token_types[hparse_i] < TOKEN_TYPE_LITERAL)) {;}
    else main_token_types[hparse_i]= proposed_type;
    main_token_reftypes[hparse_i]= reftype;
    hparse_expected= "";
    hparse_i_of_last_accepted= hparse_i;
    hparse_f_nexttoken();
    ++hparse_count_of_accepts;
    return 1;
  }
  /* these 2 lines are duplicated in hparse_f_accept_dotted() */
  if (hparse_expected > "") hparse_expected.append(" or ");
  hparse_expected.append(hparse_f_token_to_appendee(token, reftype));
  return 0;
}

/*
  Replace [identifier] with something more specific.
  Todo: there are some problems with what-is-an-identifier calculation:
  TOKEN_REFTYPE_ANY can be for @ within a user
  TOKEN_REFTYPE_HOST and TOKEN_REFTYPE_USER can be for literals
  "*" can be TOKEN_REFTYPE_DATABASE and TOKEN_TYPE_IDENTIFIER
*/
QString MainWindow::hparse_f_token_to_appendee(QString token, int reftype)
{
  QString appendee= token;
  if (token != "[identifier]") return appendee;
  if (reftype == TOKEN_REFTYPE_ALIAS) appendee= "[alias identifier]";
  else if (reftype == TOKEN_REFTYPE_CHANNEL) appendee= "[channel identifier]";
  else if (reftype == TOKEN_REFTYPE_CHARACTER_SET) appendee= "[character set identifier]";
  else if (reftype == TOKEN_REFTYPE_COLLATION) appendee= "[collation identifier]";
  else if (reftype == TOKEN_REFTYPE_COLUMN) appendee= "[column identifier]";
  else if (reftype == TOKEN_REFTYPE_COLUMN_OR_USER_VARIABLE) appendee= "[column or user variable identifier]";
  else if (reftype == TOKEN_REFTYPE_COLUMN_OR_VARIABLE) appendee= "[column or variable identifier]";
  else if (reftype == TOKEN_REFTYPE_CONDITION_DEFINE) appendee= "[condition identifier]";
  else if (reftype == TOKEN_REFTYPE_CONDITION_REFER) appendee= "[condition identifier]";
  else if (reftype == TOKEN_REFTYPE_CONDITION_OR_CURSOR) appendee= "[condition or cursor identifier]";
  else if (reftype == TOKEN_REFTYPE_CONSTRAINT) appendee= "[constraint identifier]";
  else if (reftype == TOKEN_REFTYPE_CURSOR_DEFINE) appendee= "[cursor identifier]";
  else if (reftype == TOKEN_REFTYPE_CURSOR_REFER) appendee= "[cursor identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE) appendee= "[database identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_CONSTRAINT) appendee= "[database|constraint identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_EVENT) appendee= "[database|event identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_FUNCTION) appendee= "[database|function identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_FUNCTION_OR_PROCEDURE) appendee= "[database|function | procedure identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_PROCEDURE) appendee= "[database|procedure identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_TABLE) appendee= "[database|table identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_TABLE_OR_COLUMN) appendee= "[database|table|column identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_TABLE_OR_COLUMN_OR_FUNCTION) appendee= "[database|table|column|function identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_TABLE_OR_COLUMN_OR_FUNCTION_OR_VARIABLE) appendee= "[database|table|column|function|variable identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_TRIGGER) appendee= "[database|trigger identifier]";
  else if (reftype == TOKEN_REFTYPE_DATABASE_OR_VIEW) appendee= "[database|view identifier]";
  else if (reftype == TOKEN_REFTYPE_ENGINE) appendee= "[engine identifier]";
  else if (reftype == TOKEN_REFTYPE_EVENT) appendee= "[event identifier]";
  else if (reftype == TOKEN_REFTYPE_FUNCTION) appendee= "[function identifier]";
  else if (reftype == TOKEN_REFTYPE_FUNCTION_OR_PROCEDURE) appendee= "[function|procedure identifier]";
  else if (reftype == TOKEN_REFTYPE_FUNCTION_OR_VARIABLE) appendee= "[function|variable identifier]";
  else if (reftype == TOKEN_REFTYPE_HANDLER_ALIAS) appendee= "[handler alias identifier]";
  else if (reftype == TOKEN_REFTYPE_HOST) appendee= "[host identifier]";
  else if (reftype == TOKEN_REFTYPE_INDEX) appendee= "[index identifier]";
  else if (reftype == TOKEN_REFTYPE_INTRODUCER) appendee= "[introducer]";
  else if (reftype == TOKEN_REFTYPE_KEY_CACHE) appendee= "[key cache identifier]";
  else if (reftype == TOKEN_REFTYPE_LABEL_DEFINE) appendee= "[label identifier]";
  else if (reftype == TOKEN_REFTYPE_LABEL_REFER) appendee= "[label identifier]";
  else if (reftype == TOKEN_REFTYPE_PARAMETER) appendee= "[parameter identifier]";
  else if (reftype == TOKEN_REFTYPE_PARSER) appendee= "[parser identifier]";
  else if (reftype == TOKEN_REFTYPE_PARTITION) appendee= "[partition identifier]";
  else if (reftype == TOKEN_REFTYPE_PLUGIN) appendee= "[plugin identifier]";
  else if (reftype == TOKEN_REFTYPE_PROCEDURE) appendee= "[procedure identifier]";
  else if (reftype == TOKEN_REFTYPE_ROLE) appendee= "[role identifier]";
  else if (reftype == TOKEN_REFTYPE_SAVEPOINT) appendee= "[savepoint identifier]";
  else if (reftype == TOKEN_REFTYPE_SERVER) appendee= "[server identifier]";
  else if (reftype == TOKEN_REFTYPE_STATEMENT) appendee= "[statement identifier]";
  else if (reftype == TOKEN_REFTYPE_SUBPARTITION) appendee= "[subpartition identifier]";
  else if (reftype == TOKEN_REFTYPE_TABLE) appendee= "[table identifier]";
  else if (reftype == TOKEN_REFTYPE_TABLE_OR_COLUMN) appendee= "[table|column identifier]";
  else if (reftype == TOKEN_REFTYPE_TABLE_OR_COLUMN_OR_FUNCTION) appendee= "[table|column|function identifier]";
  else if (reftype == TOKEN_REFTYPE_TABLESPACE) appendee= "[tablespace identifier]";
  else if (reftype == TOKEN_REFTYPE_TRIGGER) appendee= "[trigger identifier]";
  else if (reftype == TOKEN_REFTYPE_USER) appendee= "[user identifier]";
  else if (reftype == TOKEN_REFTYPE_USER_VARIABLE) appendee= "[user variable identifier]";
  else if (reftype == TOKEN_REFTYPE_VIEW) appendee= "[view identifier]";
  else if (reftype == TOKEN_REFTYPE_VARIABLE) appendee= "[variable identifier]";
  else if (reftype == TOKEN_REFTYPE_VARIABLE_DEFINE) appendee= "[variable identifier]";
  else if (reftype == TOKEN_REFTYPE_VARIABLE_REFER) appendee= "[variable identifier]";
  else if (reftype == TOKEN_REFTYPE_WRAPPER) appendee= "[wrapper identifier]";
  return appendee;
}

/* A variant of hparse_f_accept for debugger keywords which can be shortened to n letters */
/* TODO: are you checking properly for eof or ; ??? */
int MainWindow::hparse_f_acceptn(int proposed_type, QString token, int n)
{
  if (hparse_errno > 0) return 0;
  QString token_to_compare;
  int len= hparse_token.length();
  if ((len >= n) && (len < token.length())) token_to_compare= token.left(len);
  else token_to_compare= token;
  if (QString::compare(hparse_token, token_to_compare, Qt::CaseInsensitive) == 0)
  {
    main_token_types[hparse_i]= proposed_type;
    hparse_expected= "";
    hparse_f_nexttoken();
    return 1;
  }
  if (hparse_expected > "") hparse_expected.append(" or ");
  hparse_expected.append(token);
  return 0;
}

/* expect means: if current == expected then get next and return 1; else error */
int MainWindow::hparse_f_expect(unsigned short int flag_version, unsigned char reftype,int proposed_type, QString token)
{
  if (hparse_errno > 0) return 0;
  if ((hparse_dbms_mask & flag_version) == 0) return 0;
  if (hparse_f_accept(flag_version, reftype,proposed_type, token)) return 1;
  hparse_f_error();
  return 0;
}

/* [literal] or _introducer [literal], return 1 if true */
/* todo: this is also accepting {ODBC junk} or NULL, sometimes when it shouldn't. */
/* todo: in fact it's far far too lax, you should pass what's acceptable data type */
int MainWindow::hparse_f_literal()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INTRODUCER,TOKEN_TYPE_KEYWORD, "[introducer]") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "{") == 1)
  {
    /* I can't imagine how {oj ...} could be valid if we're looking for a literal */
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "D") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "T") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TS") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "}");
      if (hparse_errno > 0) return 0;
      return 1;
    }
    else hparse_f_error();
    return 0;
  }
  else if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_NULL, "NULL") == 1)
        || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_TRUE, "TRUE") == 1)
        || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FALSE, "FALSE") == 1))
  {
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) return 1;
  /* DATE '...' | TIME '...' | TIMESTAMP '...' are literals, but DATE|TIME|TIMESTAMP are not. */
  QString hpu= hparse_token.toUpper();
  if ((hpu == "DATE") || (hpu == "TIME") || (hpu == "TIMESTAMP"))
  {
    hparse_f_next_nexttoken();
    if ((hparse_next_token.mid(0,1) == "\"") || (hparse_next_token.mid(0,1) == "'"))
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATE") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIME") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIMESTAMP") == 1))
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        if (hparse_errno > 0) return 0;
        return 1;
      }
    }
  }
  return 0;
}

/*
  DEFAULT is a reserved word which, as an operand, might be
  () the right side of an assignment for INSERT/REPLACE/UPDATE
  () the beginning of DEFAULT(col_name)
*/
int MainWindow::hparse_f_default(int who_is_calling)
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DEFAULT, "DEFAULT") == 1)
  {
    bool parenthesis_seen= false;
    if ((who_is_calling == TOKEN_KEYWORD_INSERT)
     || (who_is_calling == TOKEN_KEYWORD_UPDATE)
     || (who_is_calling == TOKEN_KEYWORD_REPLACE))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1) parenthesis_seen= true;
    }
    else
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
      if (hparse_errno > 0) return 0;
      parenthesis_seen= true;
    }
    if (parenthesis_seen == true)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
    }
    return 1;
  }
  return 0;
}

/*
  Beware: we treat @ as a separator so 'a' @ 'b' is a user name.
  MySQL doesn't expect spaces. But I'm thinking it won't cause ambiguity.
*/
int MainWindow::hparse_f_user_name()
{
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_USER,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_USER,TOKEN_TYPE_LITERAL, "[literal]") == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "@") == 1)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_HOST,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_HOST,TOKEN_TYPE_LITERAL, "[literal]") == 1)) {;}
    }
    else if ((hparse_token.mid(0, 1) == "@")
          && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_HOST,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1))
    {
      ;
    }
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_USER") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
    }
    return 1;
  }
  return 0;
}

/*
  It's possible for a user to define a new character set, but
  we only check the official names. An undocumented "feature"
  is that users can pass a string literal, we won't check it.
*/
int MainWindow::hparse_f_character_set_name()
{
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ARMSCII8") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASCII") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BIG5") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP1250") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP1251") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP1256") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP1257") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP850") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP852") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP866") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CP932") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEC8") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EUCJPMS") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EUCKR") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FILENAME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GB2312") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GBK") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GEOSTD8") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GREEK") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HEBREW") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HP8") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEYBCS2") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KOI8R") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KOI8U") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LATIN1") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LATIN2") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LATIN5") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LATIN7") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MACCE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MACROMAN") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SJIS") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SWE7") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIS620") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UCS2") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UJIS") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTF16") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTF16LE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTF32") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTF8") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTF8MB4") == 1))
    return 1;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) return 1;
  return 0;
}

/* Todo: someday check collation names the way we check character set names. */
int MainWindow::hparse_f_collation_name()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLLATION,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1) return 1;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) return 1;
  return 0;
}

/*
  Routines starting with hparse_f_table... are based on
  https://dev.mysql.com/doc/refman/5.5/en/join.html
*/

/*
  In the following explanatory lists, ID means IDENTIFIER,
  BLANK means end-of-input, OTHER means non-blank-non-dot.
  qualified_name_of_object:
    "." ID                . object (MySQL/MariaDB tables only)
    ID "." ID             database . object
    ID "." BLANK          database . expected-object
    ID BLANK              database|object
    ID OTHER              object
  qualified_name_of_column: see hparse_f_qualified_name_of_operand()
  qualified_name_of_star:
    "*"                   column
    ID "." "*"            table . column
    ID "." ID "." "*"     database . table . column
*/

/*
  For names which might be qualified by [database_name]." namely:
  event function procedure table trigger view. not index. not column.
  e.g. we might pass TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE
*/
int MainWindow::hparse_f_qualified_name_of_object(int database_or_object_identifier, int object_identifier)
{ 
  if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0)
    && (object_identifier == TOKEN_REFTYPE_TABLE)
    && (hparse_token == "."))
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
    main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  hparse_f_next_nexttoken();
  if (hparse_next_token == ".")
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ".");
    if (hparse_errno > 0) return 0;
    if (object_identifier == TOKEN_REFTYPE_TABLE)
    {
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
    }
    hparse_f_expect(FLAG_VERSION_ALL, object_identifier,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  if (hparse_next_token == "")
  {
    if (hparse_f_accept(FLAG_VERSION_ALL, database_or_object_identifier,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      return 1;
    }
    return 0;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, object_identifier,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    return 1;
  }
  return 0;
}

/*
  Variant of hparse_f_qualified_name_of_object,"*" is acceptable.
  For GRANT and REVOKE.
*/
int MainWindow::hparse_f_qualified_name_of_object_with_star(int database_or_object_identifier, int object_identifier)
{
  hparse_f_next_nexttoken();
  if (hparse_next_token == ".")
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "*") == 0)
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ".");
    if (hparse_errno > 0) return 0;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, object_identifier,TOKEN_TYPE_IDENTIFIER, "*") == 0)
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, object_identifier,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, object_identifier,TOKEN_TYPE_IDENTIFIER, "*") == 1)
  {
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, database_or_object_identifier,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    return 1;
  }
  return 0;
}


/*
  We're looking for a site identifier, but wow it gets complicated.
  In this chart:
  (MySQL) means MySQL or MariaDB as opposed to SQLite,
  (v) means a variable is possible because we're in a routine,
  (s) means a column is possible because we're inside DML.
  "Situation" is what's in the next few tokens, X being identifier.
  "Expect" is what we pass to hparse_f_expect() if Situation is true.
  Situation                                Expect
  ---------                                ------
  .           (MySQL)                 (s)  . table . column
  @@X EOF     (MySQL) (operand)            variable
  @@X . EOF   (MySQL) (operand)            variable . variable
  @@X . X .   (MySQL) (operand)            variable . variable . variable
  @@X . X     (MySQL) (operand)            variable . variable
  @X          (MySQL) (operand)            variable
  X EOF       (MySQL) (operand) (v)   (s)  database|table|column|function|variable
  X EOF       (MySQL) (operand) (v)        function|variable
  X EOF               (operand)       (s)  database|table|column|function
  X EOF                               (s)  database|table|column
  X EOF       (MySQL) (operand)            function
  X (                 (operand)            function
  X . EOF     (MySQL) (operand)       (s)  database|table . table|column|function
  X . EOF                             (s)  database|table . table|column
  X . X EOF   (MySQL) (operand)       (s)  database|table . table|column|function
  X . X EOF                           (s)  database|table . table|column
  X . X .                             (s)  database . table . column
  X . X other                         (s)  table . column
  X other     (MySQL) (operand) (v)   (s)  column|variable
  X other     (MySQL) (operand) (v)        variable
  X other     (MySQL) (operand)       (s)  column
  X other                             (s)  column
  And in MySQL, if X is qualified, it's okay even if it's reserved.
  Todo: "drop table .www;" is legal but you don't allow it (that's for object names).
  We set v iff MySQL/MariaDB AND there might be parameters or variables.
*/
int MainWindow::hparse_f_qualified_name_of_operand(bool o)
{
  bool m= false;
  bool s= false;
  bool v= false;
  if (hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) m= true;
  if ((hparse_statement_type == TOKEN_KEYWORD_INSERT)
   || (hparse_statement_type == TOKEN_KEYWORD_DELETE)
   || (hparse_statement_type == TOKEN_KEYWORD_UPDATE)
   || (hparse_statement_type == TOKEN_KEYWORD_REPLACE)
   || (hparse_statement_type == TOKEN_KEYWORD_CREATE)
   || (hparse_statement_type == TOKEN_KEYWORD_ALTER)
   || (hparse_statement_type == TOKEN_KEYWORD_HANDLER)
   || (hparse_statement_type == TOKEN_KEYWORD_LOAD)
   || (hparse_statement_type == TOKEN_KEYWORD_SELECT)) s= true;
  if (m)
  {
    if (hparse_f_variables(false) > 0) v= true;
  }
  hparse_f_next_nexttoken();
  if (m & s)
  {
    if (hparse_token == ".")
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o)
  {
    if ((hparse_token.left(2) == "@@")
     && (hparse_next_token == ""))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o)
  {
    if ((hparse_token.left(2) == "@@")
     && (hparse_next_token == ".")
     && (hparse_next_next_token != "")
     && (hparse_next_next_next_token == "."))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o)
  {
    if ((hparse_token.left(2) == "@@")
     && (hparse_next_token == ".")
     && (hparse_next_next_token != ""))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o)
  {
    if (hparse_token.left(1) == "@")
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  /* If hparse_f_accept() succeeds, we have "X" and it's not blank.
     We might change reftype later in this function. */
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
    return 0;
  main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
  if (m & o & v & s)
  {
    if (hparse_next_token == "")
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE_OR_TABLE_OR_COLUMN_OR_FUNCTION_OR_VARIABLE;
      return 1;
    }
  }
  if (m & o & v)
  {
    if (hparse_next_token == "")
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_FUNCTION_OR_VARIABLE;
      return 1;
    }
  }
  if (o & s)
  {
    if (hparse_next_token == "")
    {
      main_token_reftypes[hparse_i_of_last_accepted]=  TOKEN_REFTYPE_DATABASE_OR_TABLE_OR_COLUMN_OR_FUNCTION;
      return 1;
    }
  }
  if (s)
  {
    if (hparse_next_token == "")
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE_OR_TABLE_OR_COLUMN;
      return 1;
    }
  }
  if (m & o)
  {
    if (hparse_next_token == "")
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_FUNCTION;
      return 1;
    }
  }
  {
    if (hparse_next_token == "(")
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_FUNCTION;
      return 1;
    }
  }
  if (m & o & s)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token == ""))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE_OR_TABLE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE_OR_COLUMN_OR_FUNCTION, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (s)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token == ""))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE_OR_TABLE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE_OR_COLUMN, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o & s)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token != "")
     && (hparse_next_next_next_token == ""))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE_OR_TABLE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE_OR_COLUMN_OR_FUNCTION, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (s)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token != "")
     && (hparse_next_next_next_token == ""))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE_OR_TABLE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE_OR_COLUMN, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (s)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token != "")
     && (hparse_next_next_next_token == "."))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token != "")
     && (hparse_next_next_next_token == "("))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_DATABASE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_FUNCTION, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (s)
  {
    if ((hparse_next_token == ".")
     && (hparse_next_next_token != ""))
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_TABLE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return 0;
      if (m)
      {
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
        main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  if (m & o & v & s)
  {
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_COLUMN_OR_VARIABLE;
      return 1;
    }
  }
  if (m & o & v)
  {
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_VARIABLE;
      return 1;
    }
  }
  if (s)
  {
    {
      main_token_reftypes[hparse_i_of_last_accepted]= TOKEN_REFTYPE_COLUMN;
      return 1;
    }
  }
  return 1;
}

int MainWindow::hparse_f_qualified_name_with_star() /* like hparse_f_qualified_name but may end with * */
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "*") == 1) return 1;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".") == 1)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "*") == 1) return 1;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return 0;
      }
    }
    return 1;
  }
  return 0;
}

/* escaped_table_reference [, escaped_table_reference] ... */
int MainWindow::hparse_f_table_references()
{
  int saved_hparse_i= hparse_i;
  do
  {
    hparse_f_table_escaped_table_reference();
    if (hparse_errno > 0) return 0;
  } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  if (hparse_i == saved_hparse_i) return 0;
  return 1;
}

/* table_reference | { OJ table_reference } */
void MainWindow::hparse_f_table_escaped_table_reference()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "{") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OJ");
    if (hparse_errno > 0) return;
    if (hparse_f_table_reference(0) == 0)
    {
      hparse_f_error();
      return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "}");
    if (hparse_errno > 0) return;
    return;
  }
  if (hparse_f_table_reference(0) == 1) return;
  if (hparse_errno > 0) return;
}

/* table_factor | join_table
   Since join_table might start with table_factor, we might have to back up and redo.
*/
int MainWindow::hparse_f_table_reference(int who_is_calling)
{
  {
    int saved_hparse_i= hparse_i;
    int saved_hparse_token_type= hparse_token_type;
    QString saved_hparse_token= hparse_token;

    if (hparse_f_table_factor() == 1)
    {
      /* todo: figure out whether the word OUTER is on this list correctly */
      if (who_is_calling == TOKEN_KEYWORD_JOIN) return 1;
      if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_INNER, "INNER") == 1)
       || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CROSS, "CROSS") == 1)
       || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_JOIN, "JOIN") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_STRAIGHT_JOIN, "STRAIGHT_JOIN") == 1)
       || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LEFT, "LEFT") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RIGHT, "RIGHT") == 1)
       || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OUTER, "OUTER") == 1)
       || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_NATURAL, "NATURAL") == 1))
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        hparse_i= saved_hparse_i;
        hparse_token_type= saved_hparse_token_type;
        hparse_token= saved_hparse_token;
        if (hparse_f_table_join_table() == 1)
        {
          /* Despite the BNF MySQL accepts a series of LEFTs and RIGHTs */
          /* todo: check for other cases where MySQL accepts a series */
          for (;;)
          {
            if ((QString::compare(hparse_token, "LEFT", Qt::CaseInsensitive) == 0)
             || (QString::compare(hparse_token, "RIGHT", Qt::CaseInsensitive) == 0)
             || (QString::compare(hparse_token, "NATURAL", Qt::CaseInsensitive) == 0))
            {
              if (hparse_f_table_join_table() == 0) break;
              if (hparse_errno > 0) return 0;
            }
            else break;
          }
          return 1;
        }
        hparse_f_error();
        return 0;
      }
    }
    return 1;
  }
  hparse_f_error();
  return 0;
}

/* tbl_name [PARTITION (partition_names)]
        [[AS] alias] [index_hint_list]
   | table_subquery [AS] alias
   | ( table_references ) */
/* Undocumented detail: alias can be a literal instead of an identifier. Ugly. */
int MainWindow::hparse_f_table_factor()
{
  if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE,TOKEN_REFTYPE_TABLE) == 1)
  {
    hparse_f_partition_list(false, false);
    if (hparse_errno > 0) return 0;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_AS, "AS") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
    }
    else
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
        hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
    }
    hparse_f_table_index_hint_list();
    if (hparse_errno > 0) return 0;
    return 1;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    if (hparse_f_select(false) == 1)
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
      hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_AS, "AS");
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      return 1;
    }
    else
    {
      if (hparse_errno > 0) return 0;
      hparse_f_table_references();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
      return 1;
    }
  }
  return 0;
}

/*
  table_reference [INNER | CROSS] JOIN table_factor [join_condition]
  | table_reference STRAIGHT_JOIN table_factor
  | table_reference STRAIGHT_JOIN table_factor ON conditional_expr
  | table_reference {LEFT|RIGHT} [OUTER] JOIN table_reference join_condition
  | table_reference NATURAL [{LEFT|RIGHT} [OUTER]] JOIN table_factor
  ...  we've changed the first choice to
  table_reference { [INNER | CROSS] JOIN table_reference [join_condition] ... }
*/
int MainWindow::hparse_f_table_join_table()
{
  if (hparse_f_table_reference(TOKEN_KEYWORD_JOIN) == 1)
  {
    bool inner_or_cross_seen= false;
    for (;;)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_INNER, "INNER") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CROSS, "CROSS") == 1))
      {
        inner_or_cross_seen= true;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_JOIN, "JOIN");
        if (hparse_errno > 0) return 0;
        if (hparse_f_table_factor() == 0)
        {
           hparse_f_error();
           return 0;
        }
        hparse_f_table_join_condition();
        if (hparse_errno > 0) return 0;
      }
      else break;
    }
    if (inner_or_cross_seen == true) return 1;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_STRAIGHT_JOIN, "STRAIGHT_JOIN") == 1)
    {
      if (hparse_f_table_factor() == 0)
      {
         hparse_f_error();
         return 0;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_ON, "ON") == 1)
      {
        hparse_f_opr_1(0);
        if (hparse_errno > 0) return 0;
      }
      return 1;
    }
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LEFT, "LEFT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RIGHT, "RIGHT") == 1))
    {
      main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OUTER, "OUTER");
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_JOIN, "JOIN");
      if (hparse_errno > 0) return 0;
      if (hparse_f_table_reference(0) == 0)
      {
         hparse_f_error();
         return 0;
      }
      if (hparse_f_table_join_condition() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      return 1;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_NATURAL, "NATURAL"))
    {
      if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LEFT, "LEFT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RIGHT, "RIGHT") == 1))
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      else
      {
        hparse_f_error();
        return 0;
      }
      hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OUTER, "OUTER");
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_JOIN, "JOIN");
      if (hparse_errno > 0) return 0;
      if (hparse_f_table_factor() == 0)
      {
        hparse_f_error();
        return 0;
      }
      return 1;
    }
    hparse_f_error();
    return 0;
  }
  return 0;
}

int MainWindow::hparse_f_table_join_condition()
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_ON, "ON") == 1)
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return 0;
    return 1;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_USING, "USING") == 1)
  {
    hparse_f_column_list(1, 0);
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}

/*  index_hint [, index_hint] ... */
void MainWindow::hparse_f_table_index_hint_list()
{
  do
  {
    if (hparse_f_table_index_hint() == 0) return;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
}

/* USE    {INDEX|KEY} [FOR {JOIN|ORDER BY|GROUP BY}] ([index_list])
 | IGNORE {INDEX|KEY} [FOR {JOIN|ORDER BY|GROUP BY}] (index_list)
 | FORCE  {INDEX|KEY} [FOR {JOIN|ORDER BY|GROUP BY}] (index_list) */
int MainWindow::hparse_f_table_index_hint()
{
  bool use_seen= false;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_USE, "USE") == 1) use_seen= true;
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1)  {;}
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FORCE") == 1)  {;}
  else return 0;
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 0))
  {
    hparse_f_error();
    return 0;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "JOIN") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ORDER") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY") == 0)
      {
        hparse_f_error();
        return 0;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GROUP") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY") == 0)
      {
        hparse_f_error();
        return 0;
      }
    }
  }
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return 0;
  if (hparse_f_table_index_list() == 0)
  {
    if (hparse_errno > 0) return 0;
    if (use_seen == false)
    {
      hparse_f_error();
      return 0;
    }
  }
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return 0;
  return 1;
}

/* index_name [, index_name] ... */
int MainWindow::hparse_f_table_index_list()
{
  int return_value= 0;
  do
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
    if (hparse_errno > 0) return 0;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  return return_value;
}

/*
  Operators, in order of precedence as in
  http://dev.mysql.com/doc/refman/5.7/en/operator-precedence.html
  Todo: take into account: PIPES_AS_CONCAT, HIGH_NOT_PRECEDENCE (but those are server options!)
  For unary operators: eat the operator and call the upper level.
  For binary operators: call the upper level, then loop calling the upper level.
  Call hparse_f_opr_1 when you want an "expression", hparse_f_opr_18 for an "operand".
*/

/*
  TODO: I'm not sure about this, it seems to allow a := b := c
*/
void MainWindow::hparse_f_opr_1(int who_is_calling) /* Precedence = 1 (bottom) */
{
  hparse_f_opr_2(who_is_calling);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ":=") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1))
  {
    hparse_f_opr_2(who_is_calling);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_2(int who_is_calling) /* Precedence = 2 */
{
  hparse_f_opr_3(who_is_calling);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OR") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "||") == 1))
  {
    hparse_f_opr_3(who_is_calling);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_3(int who_is_calling) /* Precedence = 3 */
{
  hparse_f_opr_4(who_is_calling);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "XOR") == 1)
  {
    hparse_f_opr_4(who_is_calling);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_4(int who_is_calling) /* Precedence = 4 */
{
  hparse_f_opr_5(who_is_calling);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AND") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "&&") == 1))
  {
    hparse_f_opr_5(who_is_calling);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_5(int who_is_calling) /* Precedence = 5 */
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT") == 1) {;}
  hparse_f_opr_6(who_is_calling);
  if (hparse_errno > 0) return;
}

/*
  Re MATCH ... AGAINST: unfortunately IN is an operator but also a clause-starter.
  So if we fail because "IN (" was expected, this is the one time when we have to
  override and set hparse_errno back to zero and carry on.
  Re CASE ... END: we change the token types, trying to avoid confusion with CASE statement.
*/
void MainWindow::hparse_f_opr_6(int who_is_calling) /* Precedence = 6 */
{
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CASE_IN_CASE_EXPRESSION, "CASE") == 1)
  {
    int when_count= 0;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN") == 0)
    {
      hparse_f_opr_1(who_is_calling);
      if (hparse_errno > 0) return;
    }
    else when_count= 1;
    for (;;)
    {
      if ((when_count == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN") == 1))
      {
        ++when_count;
        hparse_f_opr_1(who_is_calling);
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "THEN") == 1)
        {
          hparse_f_opr_1(who_is_calling);
          if (hparse_errno > 0) return;
        }
        else hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else break;
    }
    if (when_count == 0)
    {
      hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ELSE") == 1)
    {
      hparse_f_opr_1(who_is_calling);
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END_IN_CASE_EXPRESSION, "END") == 1)
    {
      return;
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MATCH") == 1)
  {
    hparse_f_column_list(1, 1);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AGAINST");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    hparse_f_opr_1(who_is_calling);
    bool in_seen= false;
    if (hparse_errno > 0)
    {
      if (QString::compare(hparse_prev_token, "IN", Qt::CaseInsensitive) != 0) return;
      hparse_errno= 0;
      in_seen= true;
    }
    else
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        in_seen= true;
      }
    }
    if (in_seen == true)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BOOLEAN") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MODE");
        return;
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NATURAL");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LANGUAGE");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MODE");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXPANSION");
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXPANSION");
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }

//  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BETWEEN") == 1)
//   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CASE") == 1)
//   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN") == 1)
//   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "THEN") == 1)
//   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ELSE") == 1)
//          )
//    {;}
  hparse_f_opr_7(who_is_calling);
  if (hparse_errno > 0) return;
}

/* Most comp-ops can be chained e.g. "a <> b <> c", but not LIKE or IN. */
void MainWindow::hparse_f_opr_7(int who_is_calling) /* Precedence = 7 */
{
  if ((hparse_subquery_is_allowed == true) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS") == 1))
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    if (hparse_f_select(false) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }
  if (hparse_f_is_equal(hparse_token, "(")) hparse_f_opr_8(who_is_calling, ALLOW_FLAG_IS_MULTI);
  else hparse_f_opr_8(who_is_calling, 0);
  if (hparse_errno > 0) return;
  for (;;)
  {
    /* If we see "NOT", the only comp-ops that can follow are "LIKE" and "IN". */
    bool not_seen= false;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT") == 1)
    {
      not_seen= true;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIKE") == 1)
    {
      hparse_like_seen= true;
      hparse_f_opr_8(who_is_calling, 0);
      hparse_like_seen= false;
      break;
    }
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GLOB") == 1)
    {
      hparse_f_opr_8(who_is_calling, 0);
      break;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1)
    {
      hparse_f_opr_8(who_is_calling, ALLOW_FLAG_IS_MULTI);
      if (hparse_errno > 0) return;
      break;
    }
    /* The manual says BETWEEN has a higher priority than this */
    else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BETWEEN") == 1)
    {
      hparse_f_opr_8(who_is_calling, 0);
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AND");
      if (hparse_errno > 0) return;
      hparse_f_opr_8(who_is_calling, 0);
      if (hparse_errno > 0) return;
      return;
    }
    if (not_seen == true)
    {
      hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "->") == 1) /* MySQL 5.7.9 JSON-colum->path operator */
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<=>") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REGEXP") == 1))
    {
      hparse_f_opr_8(who_is_calling, 0);
      if (hparse_errno > 0) return;
      continue;
    }
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">=") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<=") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<>") == 1)
     || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "==") == 1)
     || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "!<") == 1)
     || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "!>") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "!=") == 1))
    {
      if (hparse_subquery_is_allowed == true)
      {
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SOME") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ANY") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1))
        {
          /* todo: what if some mad person has created a function named any or some? */
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
          if (hparse_errno > 0) return;
          if (hparse_f_select(false) == 0) hparse_f_error();
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
          if (hparse_errno > 0) return;
          continue;
        }
      }
      if (hparse_f_is_equal(hparse_token, "(")) hparse_f_opr_8(who_is_calling, ALLOW_FLAG_IS_MULTI);
      else hparse_f_opr_8(who_is_calling, 0);
      if (hparse_errno > 0) return;
      continue;
    }
    else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IS") == 1)
    {
      hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT");
      if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NULL") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRUE") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FALSE") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNKNOWN") == 1))
        {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
      continue;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SOUNDS") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIKE");
      if (hparse_errno > 0) return;
      hparse_f_opr_8(who_is_calling, 0);
      if (hparse_errno > 0) return;
      continue;
    }
    break;
  }
}

void MainWindow::hparse_f_opr_8(int who_is_calling, int allow_flags) /* Precedence = 8 */
{
  if (hparse_errno > 0) return;
  hparse_f_opr_9(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "|") == 1)
  {
    hparse_f_opr_9(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_9(int who_is_calling, int allow_flags) /* Precedence = 9 */
{
  hparse_f_opr_10(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "&") == 1)
  {
    hparse_f_opr_10(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_10(int who_is_calling, int allow_flags) /* Precedence = 10 */
{
  hparse_f_opr_11(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<<") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">>") == 1))
  {
    hparse_f_opr_11(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_11(int who_is_calling, int allow_flags) /* Precedence = 11 */
{
  hparse_f_opr_12(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "-") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "+") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_BINARY_PLUS_OR_MINUS;
    hparse_f_opr_12(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_12(int who_is_calling, int allow_flags) /* Precedence = 12 */
{
  if (hparse_errno > 0) return;
  hparse_f_opr_13(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "*") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "/") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "DIV") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "%") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "MOD") == 1))
  {
    hparse_f_opr_13(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_13(int who_is_calling, int allow_flags) /* Precedence = 13 */
{
  hparse_f_opr_14(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "^") == 1)
  {
    hparse_f_opr_14(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_opr_14(int who_is_calling, int allow_flags) /* Precedence = 14 */
{
  if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "-") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "+") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "~") == 1))
  {
    hparse_f_opr_15(who_is_calling, 0);
  }
  else hparse_f_opr_15(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_opr_15(int who_is_calling, int allow_flags) /* Precedence = 15 */
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "!") == 1)
  {
    hparse_f_opr_16(who_is_calling, 0);
  }
  else hparse_f_opr_16(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
}

/* Actually I'm not sure what ESCAPE precedence is, as long as it's higher than LIKE. */
void MainWindow::hparse_f_opr_16(int who_is_calling, int allow_flags) /* Precedence = 16 */
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY") == 1)
  {
    hparse_f_opr_17(who_is_calling, 0);
  }
  else hparse_f_opr_17(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  if (hparse_like_seen == true)
  {
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ESCAPE") == 1)
    {
      hparse_like_seen= false;
      hparse_f_opr_17(who_is_calling, 0);
      if (hparse_errno > 0) return;
      return;
    }
  }
  while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
  {
    hparse_f_opr_17(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

/* todo: disallow INTERVAL unless we've seen + or - */
void MainWindow::hparse_f_opr_17(int who_is_calling, int allow_flags) /* Precedence = 17 */
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTERVAL") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
    if (hparse_errno > 0) return;
    hparse_f_interval_quantity(TOKEN_KEYWORD_INTERVAL);
    if (hparse_errno > 0) return;
    return;
  }
  hparse_f_opr_18(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
}

/*
  Final level is operand.
  factor = identifier | number | "(" expression ")" .
*/
void MainWindow::hparse_f_opr_18(int who_is_calling, int allow_flags) /* Precedence = 18, top */
{
  if (hparse_errno > 0) return;
  QString opd= hparse_token.toUpper();
  bool identifier_seen= false;
  /* Check near the start for all built-in functions that happen to be reserved words */
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CHAR, "CHAR") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CONVERT, "CONVERT") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXPRESSION, "IF") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_INSERT, "INSERT") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LEFT, "LEFT") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOCALTIME, "LOCALTIME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOCALTIMESTAMP, "LOCALTIMESTAMP") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_MOD, "MOD") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPEAT_IN_REPEAT_EXPRESSION, "REPEAT") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPLACE, "REPLACE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RIGHT, "RIGHT") == 1))
  {
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")") == 0)
    {
      hparse_f_function_arguments(opd);
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    return;
  }

  /* TODO: This should only work for INSERT ... ON DUPLICATE KEY UPDATE */
  if ((hparse_statement_type == TOKEN_KEYWORD_INSERT)
   && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUES") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1)
        || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEMA") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }
  if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_DATE") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_TIME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_USER") == 1)
   || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_TIMESTAMP") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTC_DATE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTC_TIME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UTC_TIMESTAMP") == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    return;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATE") == 1) /* DATE 'x', else DATE is not reserved so might be an id */
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIMESTAMP") == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) return;
    identifier_seen= true;
  }
  int saved_hparse_i= hparse_i;
  hparse_f_next_nexttoken();
  if (hparse_next_token == "(")
  {
    if ((main_token_flags[hparse_i] & TOKEN_FLAG_IS_FUNCTION) != 0)
    {
      int saved_token= main_token_types[hparse_i];
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[reserved function]");
        if (hparse_errno > 0) return;
      }
      identifier_seen= true;
      main_token_types[saved_hparse_i]= saved_token;
    }
  }
  if ((identifier_seen == true)
   || (hparse_f_qualified_name_of_operand(true) == 1))
  {
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1) /* identifier followed by "(" must be a function name */
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")") == 0)
      {
        hparse_f_function_arguments(opd);
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      hparse_f_over(saved_hparse_i, who_is_calling);
      if (hparse_errno > 0) return;
    }
    return;
  }
  if (hparse_f_literal() == 1)
  {
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_errno > 0) return;
  if (hparse_f_default(TOKEN_KEYWORD_SELECT) == 1)
  {
    return;
  }
  else if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "("))
  {
    if (hparse_errno > 0) return;
    /* if subquery is allowed, check for "(SELECT ...") */
    if ((hparse_subquery_is_allowed == true)
     && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SELECT") == 1))
    {
      hparse_f_select(true);
      if (hparse_errno > 0) return;
    }
    else if ((allow_flags & ALLOW_FLAG_IS_MULTI) != 0)
    {
      int expression_count= 0;
      hparse_f_parenthesized_multi_expression(&expression_count);
    }
    else hparse_f_opr_1(who_is_calling);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }
  hparse_f_error();
  return;
}

/*
  Check for OVER () if MariaDB 10.2, and in select-list or in order-by list.
  After ROW_NUMBER() it is compulsory. After AVG() it is optional.
  TODO: this will have to be checked again when MariaDB 10.2 is released.
*/
void MainWindow::hparse_f_over(int saved_hparse_i, int who_is_calling)
{
  if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_10_2_2) == 0) return;
  if (who_is_calling != TOKEN_KEYWORD_SELECT) return;
  bool function_is_aggregate= false;
  if ((main_token_types[saved_hparse_i] == TOKEN_KEYWORD_CUME_DIST)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_DENSE_RANK)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_NTILE)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_PERCENT_RANK)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_RANK)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_ROW_NUMBER))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OVER, "OVER");
    if (hparse_errno > 0) return;
  }
  else if ((main_token_types[saved_hparse_i] == TOKEN_KEYWORD_AVG)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_BIT_AND)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_BIT_OR)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_BIT_XOR)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_COUNT)
   || (main_token_types[saved_hparse_i] == TOKEN_KEYWORD_SUM))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OVER, "OVER") == 0) return;
    function_is_aggregate= true;
  }
  else return;
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PARTITION, "PARTITION") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OVER, "BY");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "("))
      {
        hparse_f_opr_1(0);
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      else
      {
        hparse_f_opr_1(0);
        if (hparse_errno > 0) return;
      }
    }
    if ((function_is_aggregate == false)
     && (QString::compare(hparse_token, "ORDER", Qt::CaseInsensitive) != 0))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ORDER");
      if (hparse_errno > 0) return;
    }
    if ((hparse_f_order_by(0) == 1)
     && (function_is_aggregate == true))
    {
      /* window frame */
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RANGE") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROWS") == 1))
      {
        if (hparse_f_over_start(0) == 1) {;}
        else if (hparse_errno > 0) return;
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BETWEEN") == 1)
        {
          if (hparse_f_over_start(TOKEN_KEYWORD_BETWEEN) == 0) hparse_f_error();
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AND");
          if (hparse_errno > 0) return;
          if (hparse_f_over_end() == 0) hparse_f_error();
          if (hparse_errno > 0) return;
        }
        else hparse_f_error();
        if (hparse_errno > 0) return;
      }
    }
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
  }
  return;
}

int MainWindow::hparse_f_over_start(int who_is_calling)
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNBOUNDED") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRECEDING");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROW");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  if (who_is_calling != TOKEN_KEYWORD_BETWEEN) return 0;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRECEDING") == 0)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOLLOWING");
    }
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}

int MainWindow::hparse_f_over_end()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNBOUNDED") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOLLOWING");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROW");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRECEDING") == 0)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOLLOWING");
    }
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}


/*
  TODO: Recognize all 400+ built-in functions.
  Until then, we'll assume any function has a generalized comma-delimited expression list.
  But we still have to handle the ones that don't have simple lists.
*/
void MainWindow::hparse_f_function_arguments(QString opd)
{
  if ((hparse_f_is_equal(opd,"AVG"))
   || (hparse_f_is_equal(opd, "SUM"))
   || (hparse_f_is_equal(opd, "MIN"))
   || (hparse_f_is_equal(opd, "MAX")))
  {
    hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISTINCT");
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_is_equal(opd, "CAST"))
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS");
    if (hparse_errno > 0) return;
    if (hparse_f_data_type() == -1) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_is_equal(opd, "CHAR"))
  {
    do
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CHARACTER_SET,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
        break;
      }
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0) && hparse_f_is_equal(opd, "CONVERT"))
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CHARACTER_SET,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0) && hparse_f_is_equal(opd, "IF"))
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",");
    if (hparse_errno > 0) return;
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",");
    if (hparse_errno > 0) return;
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_is_equal(opd, "COUNT"))
  {
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISTINCT") == 1) hparse_f_opr_1(0);
    else
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "*") == 1) {;}
      else hparse_f_opr_1(0);
    }
    if (hparse_errno > 0) return;
  }
  else if ((hparse_f_is_equal(opd, "SUBSTR")) || (hparse_f_is_equal(opd, "SUBSTRING")))
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1))
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1))
      {
        hparse_f_opr_1(0);
        if (hparse_errno > 0) return;
      }
    }
  }
  else if (hparse_f_is_equal(opd, "TRIM"))
  {
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BOTH") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LEADING") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRAILING") == 1)) {;}
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1)
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    }
  }
  else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0) && hparse_f_is_equal(opd, "WEIGHT_STRING"))
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1)
    {
      int hparse_i_of_char= hparse_i;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHAR") == 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY");
        if (hparse_errno > 0) return;
      }
      main_token_flags[hparse_i_of_char] &= (~TOKEN_FLAG_IS_FUNCTION);
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LEVEL") == 1)
    {
      do
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASC") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DESC") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REVERSE") == 1)) {;}
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
  }
  else do
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
}

void MainWindow::hparse_f_expression_list(int who_is_calling)
{
  bool comma_is_seen;
  main_token_flags[hparse_i] |= TOKEN_FLAG_IS_START_IN_COLUMN_LIST;
  do
  {
    comma_is_seen= false;
    if (who_is_calling == TOKEN_KEYWORD_SELECT) hparse_f_next_nexttoken();
    if (hparse_errno > 0) return;
    if (hparse_f_default(who_is_calling) == 1) {;}
    else if ((who_is_calling == TOKEN_KEYWORD_SELECT) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "*") == 1)) {;}
    else if ((who_is_calling == TOKEN_KEYWORD_SELECT)
          && (hparse_f_is_equal(hparse_next_token, "."))
          && (hparse_f_is_equal(hparse_next_next_token, "*"))
          && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]")))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "*");
    }
    else
    {
      hparse_f_opr_1(who_is_calling);
    }
    if (hparse_errno > 0) return;
    if (who_is_calling == TOKEN_KEYWORD_SELECT)
    {
      bool as_seen= false;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1) as_seen= true;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) {;}
      else if (as_seen == true) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","))
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_END_IN_COLUMN_LIST;
      comma_is_seen= true;
    }
  } while (comma_is_seen);
}

/* e.g. (1,2,3) or ( (1,1), (2,2), (3,3) ) i.e. two parenthesization levels are okay */
void MainWindow::hparse_f_parenthesized_value_list()
{
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  do
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
    {
      do
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_parameter_list(int routine_type)
{
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  do
  {
    bool in_seen= false;
    if (routine_type == TOKEN_KEYWORD_PROCEDURE)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OUT") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INOUT") == 1))
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        in_seen= true;
      }
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_PARAMETER,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
    {

      if (routine_type != TOKEN_KEYWORD_LUA)
      {
        if (hparse_f_data_type() == -1) hparse_f_error();
        if (hparse_errno > 0) return;
      }
    }
    else if (in_seen == true)
    {
      hparse_f_error();
      return;
    }
  } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_parenthesized_expression()
{
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  hparse_f_opr_1(0);
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
}

/*
  Re int *expression_count:
  The point is: if there is more than 1, then this is only legal for comparisons,
  and both sides of the comparison should have the same count.
  But we aren't actually using this knowlede yet, because we don't count selection columns.
  Counting expressions in the select list is feasible, but "select *" causes difficulty.
*/
void MainWindow::hparse_f_parenthesized_multi_expression(int *expression_count)
{
  *expression_count= 0;
  //hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  //if (hparse_errno > 0) return;
  if ((hparse_subquery_is_allowed == true) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SELECT") == 1))
  {
    hparse_f_select(true);
    if (hparse_errno > 0) return;
    (*expression_count)= 2;          /* we didn't really count, but guess it's more than 1 */
  }
  else
  {
    do
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
      ++(*expression_count);
    } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  //hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  //if (hparse_errno > 0) return;
}


/* TODO: if statement_type <> TOKEN_KEYWORD_SET, disallow assignment to @@ or @ variables. */
void MainWindow::hparse_f_assignment(int statement_type)
{
  bool comma_is_seen;
  main_token_flags[hparse_i] |= TOKEN_FLAG_IS_START_IN_COLUMN_LIST;
  do
  {
    comma_is_seen= false;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "@@SESSION") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GLOBAL") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ".");
      if (hparse_errno > 0) return;
    }
    if (hparse_errno > 0) return;
    if ((statement_type == TOKEN_KEYWORD_SET) || (statement_type == TOKEN_KEYWORD_PRAGMA))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    }
    else /* TOKEN_KEYWORD_INSERT | UPDATE | LOAD */
    {
      if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
    }
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ":=");
    if (hparse_errno > 0) return;
    /* TODO: DEFAULT and ON and OFF shouldn't always be legal. */
    if (hparse_f_default(statement_type) == 1) continue;
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1) continue;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OFF") == 1) continue;
    /* TODO: VALUES should only be legal for INSERT ... ON DUPLICATE KEY */
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","))
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_END_IN_COLUMN_LIST;
      comma_is_seen= true;
    }
  } while (comma_is_seen);
}

void MainWindow::hparse_f_alter_specification()
{
  hparse_f_table_or_partition_options(TOKEN_KEYWORD_TABLE);
  if (hparse_errno > 0) return;
  bool default_seen= false;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1) default_seen= true;
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ADD") == 1))
  {
    bool column_name_is_expected= false;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN") == 1) column_name_is_expected= true;
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
      if (hparse_errno > 0) return;
      /* todo: check that hparse_f_partition_or_subpartition_definition does as expected */
      hparse_f_partition_or_subpartition_definition(TOKEN_KEYWORD_PARTITION);
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    else if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0) column_name_is_expected= true;
    else if (hparse_f_create_definition() == 3) column_name_is_expected= true;
    if (hparse_errno > 0) return;
    if (column_name_is_expected == true)
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        do
        {
          if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
          if (hparse_errno > 0) return;
          hparse_f_column_definition();
          if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      else
      {
        if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_column_definition();
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIRST") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AFTER") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
          if (hparse_errno > 0) return;
        }
      }
    }
    return;
  }
  if (default_seen == false)
  {
    if (hparse_f_algorithm_or_lock() == 1) return;
    if (hparse_errno > 0) return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALTER") == 1))
  {
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN");
    if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DROP") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT");
      if (hparse_errno > 0) return;
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ANALYZE") == 1))
  {
    if (hparse_f_partition_list(false, true) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHANGE") == 1))
  {
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN");
    if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_column_definition();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIRST") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AFTER") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    return;
  }
  /* Todo: Following is useless code. CHARACTER SET is a table_option. Error in manual? */
  if ((hparse_f_character_set() == 1))
  {
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
    if (hparse_f_character_set_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    return;
  }
  if (hparse_errno > 0) return;
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECK") == 1))
  {
    if (hparse_f_partition_list(false, true) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  /* "LOCK" is already handled by hparse_f_algorithm_or_lock() */
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COALESCE") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION");
    if (hparse_errno > 0) return;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONVERT") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
    if (hparse_errno > 0) return;
    hparse_f_character_set();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 0)
    {
      if (hparse_f_character_set_name() == 0) hparse_f_error();
    }
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
    {
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISABLE") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEYS");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISCARD") == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE") == 1) return;
    if (hparse_f_partition_list(false, true) == 0)
    {
      hparse_f_error();
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DROP") == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRIMARY") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
      if (hparse_errno > 0) return;
    }
    else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOREIGN") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_CONSTRAINT, TOKEN_REFTYPE_CONSTRAINT) == 0) hparse_f_error(); /* fk_symbol */
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION") == 1)
    {
      /* todo: maybe use if (hparse_f_partition_list(true, false) == 0) hparse_f_error(); */
      do
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PARTITION,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN");
      if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENABLE") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEYS");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXCHANGE") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PARTITION,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
    if (hparse_errno > 0) return;
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITHOUT") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALIDATION");
      if (hparse_errno > 0) return;
    }
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FORCE") == 1))
  {
    return;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IMPORT") == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE") == 1) return;
    if (hparse_f_partition_list(false, true) == 0)
    {
      hparse_f_error();
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MODIFY") == 1))
  {
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN");
    if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_column_definition();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIRST") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AFTER") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    return;
  }
  /* "LOCK" is already handled by hparse_f_algorithm_or_lock() */
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTIMIZE") == 1))
  {
    if (hparse_f_partition_list(false, true) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ORDER") == 1)) /* todo: could use modified hparse_f_order_by */
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
    if (hparse_errno > 0) return;
    do
    {
      if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASC") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DESC") == 1)) {;}
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REBUILD") == 1))
  {
    if (hparse_f_partition_list(false, true) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REMOVE") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITIONING");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RENAME") == 1))
  {
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX, TOKEN_TYPE_IDENTIFIER,"[identifier]");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX, TOKEN_TYPE_IDENTIFIER,"[identifier]");
      if (hparse_errno > 0) return;
    }
    else
    {
      if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1)) {;}
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REORGANIZE") == 1))
  {
    if (hparse_f_partition_list(false, false) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    do
    {
      hparse_f_partition_or_subpartition_definition(TOKEN_KEYWORD_PARTITION);
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPAIR") == 1))
  {
    if (hparse_f_partition_list(false, true) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRUNCATE") == 1))
  {
    if (hparse_f_partition_list(false, true) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPGRADE") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITIONING");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALIDATION");
    if (hparse_errno > 0) return;
    return;
  }
  if ((default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITHOUT") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALIDATION");
    if (hparse_errno > 0) return;
    return;
  }
}

/*
  accept "CHARACTER SET"
  but surprisingly often "CHARSET" can be used instead
*/
int MainWindow::hparse_f_character_set()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHARACTER") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHARSET") == 1) return 1;
  else return 0;
}

void MainWindow::hparse_f_alter_database()
{
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPGRADE") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATA");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DIRECTORY");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NAME");
    if (hparse_errno > 0) return;
  }
  else
  {
    bool character_seen= false, collate_seen= false;
    for (;;)
    {
      if ((character_seen == true) && (collate_seen == true)) break;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT")) {;}
      if ((character_seen == false) && (hparse_f_character_set() == 1))
      {
        character_seen= true;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=")) {;}
        if (hparse_f_character_set_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        continue;
      }
      if (hparse_errno > 0) return;
      if ((collate_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE")))
      {
        collate_seen= true;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=")) {;}
      if (hparse_f_collation_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        continue;
      }
      if ((character_seen == false) && (collate_seen == false))
      {
        hparse_f_error();
        return;
      }
      break;
    }
  }
}

void MainWindow::hparse_f_characteristics()
{
  bool comment_seen= false, language_seen= false, contains_seen= false, sql_seen= false;
  bool deterministic_seen= false;
  for (;;)
  {
    if ((comment_seen) && (language_seen) && (contains_seen) && (sql_seen)) break;
    if ((comment_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMENT")))
    {
      comment_seen= true;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      continue;
    }
    else if ((language_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LANGUAGE")))
    {
      language_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL");
      if (hparse_errno > 0) return;
      continue;
    }
    else if ((deterministic_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT")))
    {
      deterministic_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DETERMINISTIC");
      if (hparse_errno > 0) return;
      continue;
    }
    else if ((deterministic_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DETERMINISTIC")))
    {
      deterministic_seen= true;
      continue;
    }
    else if ((contains_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONTAINS")))
    {
      contains_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL");
      if (hparse_errno > 0) return;
      continue;
    }
    else if ((contains_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO")))
    {
      contains_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL");
      if (hparse_errno > 0) return;
      continue;
    }
    else if ((contains_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READS")))
    {
      contains_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATA");
      if (hparse_errno > 0) return;
      continue;
     }
    else if ((contains_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MODIFIES")))
    {
      contains_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATA");
      if (hparse_errno > 0) return;
      continue;
    }
    else if ((sql_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL")))
    {
      sql_seen= true;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SECURITY");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFINER") == 1) continue;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INVOKER");
      if (hparse_errno > 0) return;
      continue;
    }
    break;
  }
}

int MainWindow::hparse_f_algorithm_or_lock()
{
  bool algorithm_seen= false, lock_seen= false;
  for (;;)
  {
    if ((algorithm_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALGORITHM") == 1))
    {
      algorithm_seen= true;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1) {;}
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1) break;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INPLACE") == 1) break;
      if (hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COPY") == 1) break;
      if (hparse_errno > 0) return 0;
    }
    if ((lock_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCK") == 1))
    {
      lock_seen= true;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1) {;}
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1) break;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NONE") == 1) break;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SHARED") == 1) break;
      if (hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXCLUSIVE") == 1) break;
      if (hparse_errno > 0) return 0;
    }
    break;
  }
  if ((algorithm_seen == true) || (lock_seen == true)) return 1;
  return 0;
}

void MainWindow::hparse_f_definer()
{
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_USER") == 1) {;}
  else if (hparse_f_user_name() == 1) {;}
  else hparse_f_error();
}

void MainWindow::hparse_f_if_not_exists()
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
    if (hparse_errno > 0) return;
  }
}

int MainWindow::hparse_f_analyze_or_optimize(int who_is_calling,int *table_or_view)
{
  if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0) *table_or_view= TOKEN_KEYWORD_TABLE;
  else
  {
    *table_or_view= 0;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO_WRITE_TO_BINLOG") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL") == 1)) {;}
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 1) *table_or_view= TOKEN_KEYWORD_TABLE;
    else if ((who_is_calling == TOKEN_KEYWORD_REPAIR)
          && ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
          && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW") == 1))
      *table_or_view= TOKEN_KEYWORD_VIEW;
    else return 0;
  }
  do
  {
    if (*table_or_view == TOKEN_KEYWORD_TABLE)
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0)
        hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    else
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_VIEW, TOKEN_REFTYPE_VIEW) == 0)
        hparse_f_error();
    }
    if (hparse_errno > 0) return 0;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  return 1;
}

void MainWindow::hparse_f_character_set_or_collate()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASCII") == 1) {;}
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNICODE") == 1) {;}
  else if (hparse_f_character_set() == 1)
  {
    if (hparse_f_character_set_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
  {
    if (hparse_f_collation_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
}

/* Used for data type length. Might be useful for any case of "(" integer ")" */
void MainWindow::hparse_f_length(bool is_ok_if_decimal, bool is_ok_if_unsigned, bool is_ok_if_binary)
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_NOT_AFTER_SPACE;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if (is_ok_if_decimal)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1)
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
    }
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
  }
  if (is_ok_if_unsigned)
  {
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNSIGNED") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SIGNED") == 1)) {;}
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ZEROFILL");
  }
  if (is_ok_if_binary)
  {
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY");
    hparse_f_character_set_or_collate();
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_enum_or_set()
{
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  do
  {
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
  hparse_f_character_set_or_collate();
  if (hparse_errno > 0) return;
}

/*
  Todo: we are not distinguishing for the different data-type syntaxes,
  for example in CAST "UNSIGNED INT" is okay but "INT UNSIGNED" is illegal,
  while in CREATE "UNSIGNED INT" is illegal but "UNSIGNED INT" is okay.
  We allow any combination.
  Todo: also, in CAST, only DOUBLE is okay, not DOUBLE PRECISION.
*/
int MainWindow::hparse_f_data_type()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BIT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_BIT;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TINYINT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BOOLEAN") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT1") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_TINYINT;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SMALLINT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT2") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_SMALLINT;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MEDIUMINT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT3") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MIDDLEINT") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_MEDIUMINT;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT4") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_INT4;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTEGER") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_INTEGER;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BIGINT") == 1)|| (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT8") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_BIGINT;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REAL") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(true, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_REAL;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DOUBLE") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRECISION");
    hparse_f_length(true, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_DOUBLE;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FLOAT8") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(true, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_FLOAT8;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FLOAT") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FLOAT4") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(true, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_FLOAT4;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DECIMAL") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEC") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIXED") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(true, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_DECIMAL;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NUMERIC") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(true, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_NUMERIC;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNSIGNED") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SIGNED") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT") == 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTEGER");
    hparse_f_length(false, true, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_UNSIGNED;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SERIAL") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_SERIAL;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATE") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_DATE;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIME") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_TIME;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TIMESTAMP") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_TIMESTAMP;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATETIME") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_DATETIME;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "YEAR") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_YEAR;
  }
  int hparse_i_of_char= hparse_i;
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHAR") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHARACTER") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    main_token_flags[hparse_i_of_char] &= (~TOKEN_FLAG_IS_FUNCTION);
    bool byte_seen= false, varying_seen= false;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BYTE") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      byte_seen= true;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARYING") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      varying_seen= true;
    }
    if (byte_seen == false) hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    if (varying_seen == true) return TOKEN_KEYWORD_VARCHAR;
    return TOKEN_KEYWORD_CHAR;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARCHAR") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_VARCHAR;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARCHARACTER") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_VARCHARACTER;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NCHAR") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARYING");
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    return TOKEN_KEYWORD_CHAR;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NVARCHAR") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    return TOKEN_KEYWORD_CHAR;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NATIONAL") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    bool varchar_seen= false;
    hparse_i_of_char= hparse_i;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHAR") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      main_token_flags[hparse_i_of_char] &= (~TOKEN_FLAG_IS_FUNCTION);
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHARACTER") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARCHAR") == 1) varchar_seen= true;
    else hparse_f_error();
    if (hparse_errno > 0) return 0;
    if (varchar_seen == false) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARYING");
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    return TOKEN_KEYWORD_CHAR;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LONG") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARBINARY") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARCHAR") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MEDIUMTEXT") == 1))
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      hparse_f_length(false, false, false);
      if (hparse_errno > 0) return 0;
    }
    else hparse_f_error();
    return TOKEN_KEYWORD_LONG;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_BINARY;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARBINARY") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, false);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_VARBINARY;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TINYBLOB") == 1) return TOKEN_KEYWORD_TINYBLOB;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BLOB") == 1) return TOKEN_KEYWORD_BLOB;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MEDIUMBLOB") == 1) return TOKEN_KEYWORD_MEDIUMBLOB;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LONGBLOB") == 1) return TOKEN_KEYWORD_LONGBLOB;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TINYTEXT") == 1)
  {
    hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_TINYTEXT;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEXT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_MEDIUMTEXT;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MEDIUMTEXT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_MEDIUMTEXT;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LONGTEXT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_length(false, false, true);
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_LONGTEXT;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENUM") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_enum_or_set();
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_ENUM;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    hparse_f_enum_or_set();
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_SET;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "JSON") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return 0; /* todo: token_keyword */
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GEOMETRY") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_GEOMETRY;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "POINT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_POINT;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LINESTRING") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_LINESTRING;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "POLYGON") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_POLYGON;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MULTIPOINT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_MULTIPOINT;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MULTIPOLYGON") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_MULTIPOLYGON;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GEOMETRYCOLLECTION") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_GEOMETRYCOLLECTION;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LINESTRING") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_LINESTRING;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "POLYGON") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_POLYGON;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BOOL") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_BOOL;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BOOLEAN") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
    return TOKEN_KEYWORD_BOOLEAN;
  }
  /* If SQLite-style column definition, anything unreserved is acceptable. */
  if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
  {
    if ((hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
     || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[literal]") == 1))
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_DATA_TYPE;
      hparse_f_length(false, false, false);
      if (hparse_errno > 0) return 0;
      return TOKEN_KEYWORD_ALL;
    }
  }
  return -1; /* -1 means error unless SQLite-style column definition */
}

void MainWindow::hparse_f_reference_option()
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RESTRICT") == 1) {;}
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CASCADE") == 1) {;}
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NULL");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO") == 1)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ACTION");
    if (hparse_errno > 0) return;
  }
  else hparse_f_error();
  if (hparse_errno > 0) return;
}


void MainWindow::hparse_f_reference_definition()
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REFERENCES") == 1)
  {
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_column_list(0, 0);
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MATCH") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FULL") == 1) {;}
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTIAL") == 1) {;}
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SIMPLE") == 1) {;}
      else hparse_f_error();
    }
    bool on_delete_seen= false, on_update_seen= false;
    while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1)
    {
      if ((on_delete_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELETE") == 1))
      {
        hparse_f_reference_option();
        if (hparse_errno > 0) return;
        on_delete_seen= true;
      }
      else if ((on_update_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE") == 1))
      {
        hparse_f_reference_option();
        if (hparse_errno > 0) return;
        on_update_seen= true;
      }
      else hparse_f_error();
    }
  }
}

/*
     {INDEX|KEY}                    [index_name] [index_type] (index_col_name,...) [index_option] ...
     {FULLTEXT|SPATIAL} [INDEX|KEY] [index_name]              (index_col_name,...) [index_option] ...
  [] PRIMARY KEY                    [index_name  [index_type] (index_col_name,...) [index_option] ...
  [] UNIQUE             [INDEX|KEY] [index_name] [index_type] (index_col_name,...) [index_option] ...
  [] FOREIGN KEY                    [index_name]              (index_col_name,...) reference_definition
  [] CHECK (expression)
  In the above chart, [] is short for [CONSTRAINT x].
  The manual says [] i.e. [CONSTRAINT x] is not allowed for CHECK; actually it is; ignored.
  The manual says [index_name] is not allowed for PRIMARY KEY, actually it is, ignored.
  Return 1 if valid constraint definition, 2 if error, 3 if nothing (probably data type).
*/
int MainWindow::hparse_f_create_definition()
{
  bool constraint_seen= false;
  bool fulltext_seen= false, foreign_seen= false;
  bool unique_seen= false, check_seen= false, primary_seen= false;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONSTRAINT") == 1)
  {
    hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_CONSTRAINT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    constraint_seen= true;
  }
  if ((constraint_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1)) {;}
  else if ((constraint_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1)) {;}
  else if ((constraint_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FULLTEXT") == 1)) fulltext_seen= true;
  else if ((constraint_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SPATIAL") == 1)) fulltext_seen= true;
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRIMARY") == 1)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
    if (hparse_errno > 0) return 2;
    primary_seen= true;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNIQUE") == 1) unique_seen= true;
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOREIGN") == 1)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
    if (hparse_errno > 0) return 2;
    foreign_seen= true;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECK") == 1) check_seen= true;
  else return 3;
  if (check_seen == true)
  {
    hparse_f_parenthesized_expression();
    if (hparse_errno > 0) return 2;
    return 1;
  }
  if ((fulltext_seen == true) || (unique_seen == true))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1) {;}
    else hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
  }
  hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_CONSTRAINT, TOKEN_REFTYPE_CONSTRAINT);
  if (hparse_errno > 0) return 2;
  hparse_f_index_columns(TOKEN_KEYWORD_TABLE, fulltext_seen, foreign_seen);
  if (hparse_errno > 0) return 2;

  if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
  {
    if ((primary_seen == true) || (unique_seen == true))
    {
      hparse_f_conflict_clause();
      if (hparse_errno > 0) return 2;
    }
  }

  return 1;
}

/*
  In column_definition, after datetime|timestamp default|on update,
  current_timestamp or one of its synonyms might appear. Ugly.
  Asking for 0-6 may be too fussy, MySQL accepts 9 but ignores it.
*/
int MainWindow::hparse_f_current_timestamp()
{
  int keyword;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT_TIMESTAMP") == 1) keyword= TOKEN_KEYWORD_CURRENT_TIMESTAMP;
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCALTIME") == 1) keyword= TOKEN_KEYWORD_LOCALTIME;
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCALTIMESTAMP") == 1) keyword= TOKEN_KEYWORD_LOCALTIMESTAMP;
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOW") == 1) keyword= TOKEN_KEYWORD_NOW;
  else return 0;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "0") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "1") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "2") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "3") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "4") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "5") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "6") == 1) {;}
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, ")");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  else if (keyword == TOKEN_KEYWORD_NOW) hparse_f_error();
  if (hparse_errno > 0) return 0;
  return 1;
}

/*
  The clause order for column definitions is what MySQL 5.7
  accepts, which differs from what the MySQL 5.7 manual says.
*/
void MainWindow::hparse_f_column_definition()
{
  int data_type= hparse_f_data_type();
  if (data_type == -1) hparse_f_error();
  if (hparse_errno > 0) return;
  bool generated_seen= false;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GENERATED") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALWAYS");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS");
    if (hparse_errno > 0) return;
    generated_seen= true;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1)
  {
    generated_seen= true;
  }
  if (generated_seen == true)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIRTUAL") == 0)
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PERSISTENT");
    }
    if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIRTUAL") == 0)
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STORED");
    }
  }
  bool null_seen= false, default_seen= false, auto_increment_seen= false;
  bool unique_seen= false, primary_seen= false, comment_seen= false, column_format_seen= false;
  bool on_seen= false;
  for (;;)
  {
    if ((null_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT") == 1))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NULL");
      if (hparse_errno > 0) return;
      null_seen= true;
    }
    else if ((null_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NULL") == 1))
    {
      null_seen= true;
      hparse_f_conflict_clause();
      if (hparse_errno > 0) return;
    }
    else if ((generated_seen == false) && (default_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1))
    {
      if (((data_type == TOKEN_KEYWORD_DATETIME) || (data_type == TOKEN_KEYWORD_TIMESTAMP))
          && (hparse_f_current_timestamp() == 1)) {;}
      else if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      default_seen= true;
    }
    else if ((generated_seen == false) && (auto_increment_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AUTO_INCREMENT") == 1))
    {
      auto_increment_seen= true;
    }
    else if ((generated_seen == false) && (auto_increment_seen == false) && (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AUTOINCREMENT") == 1))
    {
      auto_increment_seen= true;
    }
    else if ((unique_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNIQUE") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
      unique_seen= true;
      hparse_f_conflict_clause();
      if (hparse_errno > 0) return;
    }
    else if ((primary_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRIMARY") == 1))
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
      if (hparse_errno > 0) return;
      primary_seen= true;
      hparse_f_conflict_clause();
      if (hparse_errno > 0) return;
    }
    else if ((primary_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1))
    {
      primary_seen= true;
    }
    else if ((comment_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMENT") == 1))
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      comment_seen= true;
    }
    else if ((generated_seen == false) && (column_format_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN_FORMAT") == 1))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIXED") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DYNAMIC") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
      column_format_seen= true;
    }
    else if ((on_seen == false) && (generated_seen == false)
             && ((data_type == TOKEN_KEYWORD_TIMESTAMP) || (data_type == TOKEN_KEYWORD_DATETIME))
             && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE");
      if (hparse_errno > 0) return;
      if (hparse_f_current_timestamp() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      on_seen= true;
    }
    else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECK") == 1)
    {
      hparse_f_parenthesized_expression();
      if (hparse_errno > 0) return;
    }
    else break;
  }
  if (generated_seen == false)
  {
    hparse_f_reference_definition();
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_comment()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMENT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_column_list(int is_compulsory, int is_maybe_qualified)
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 0)
  {
    if (is_compulsory == 1) hparse_f_error();
    return;
  }
  do
  {
    if (is_maybe_qualified == 0)
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else
    {
      if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
    }
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
}

/*
  engine = engine_name part of either CREATE TABLE or CREATE TABLESPACE.
  Usually it will be a standard engine like MyISAM or InnoDB, but with MariaDB
  there are usually more choices ... in the end, we allow any identifier.
  Although it's undocumented, ENGINE = 'literal' is okay too.
*/
void MainWindow::hparse_f_engine()
{
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1) {;}
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "ARCHIVE") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "CSV") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "EXAMPLE") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "FEDERATED") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "HEAP") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "INNODB") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "MEMORY") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "MERGE") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "MYISAM") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "NDB") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "[literal]") == 1) {;}
    else hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
}

void MainWindow::hparse_f_table_or_partition_options(int keyword)
{
  bool comma_seen= false;
  for (;;)
  {
    if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AUTO_INCREMENT") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AVG_ROW_LENGTH") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_character_set() == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_character_set_name();
      if (hparse_errno > 0) return;
    }
    else if (hparse_errno > 0) return;
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECKSUM") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMENT") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMPRESSION")) == 1)
    {
      /* todo: should be: zlib, lz4, or none */
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONNECTION")) == 1)
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATA") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DIRECTORY");
      if (hparse_errno > 0) return;
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1))
    {
      if (hparse_f_character_set() == 1)
      {
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
        hparse_f_character_set_name();
        if (hparse_errno > 0) return;
      }
      else if (hparse_errno > 0) return;
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1)
      {
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
        if (hparse_f_collation_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELAY_KEY_WRITE") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1");
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENCRYPTED") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "YES") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO");
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0) && (keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENCRYPTION") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENCRYPTION_KEY_ID") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINE") == 1))
    {
      hparse_f_engine();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IETF_QUOTES") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "YES") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO");
      if (hparse_errno > 0) return;
    }
    else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DIRECTORY");
      if (hparse_errno > 0) return;
      {
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        if (hparse_errno > 0) return;
      }
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INSERT_METHOD") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIRST") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LAST") == 1)) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY_BLOCK_SIZE") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAX_ROWS") == 1)
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MIN_ROWS") == 1)
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PACK_KEYS") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1)) {;}
      else hparse_f_error();
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PAGE_CHECKSUM") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1");
      else hparse_f_error();
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROW_FORMAT") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DYNAMIC") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIXED") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMPRESSED") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REDUNDANT") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMPACT") == 1)
       || (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PAGE") == 1)))
        {;}
      else hparse_f_error();
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATS_AUTO_RECALC") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1)) {;}
      else hparse_f_error();
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATS_PERSISTENT") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1)) {;}
      else hparse_f_error();
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATS_SAMPLE_PAGES") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DEFAULT, "DEFAULT") == 0)
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_PARTITION) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STORAGE") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINE");
      if (hparse_errno > 0) return;
      hparse_f_engine();
      if (hparse_errno > 0) return;
    }
    else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLESPACE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRANSACTIONAL") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1");
      if (hparse_errno > 0) return;
    }
    else if ((keyword == TOKEN_KEYWORD_TABLE) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNION") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
      if (hparse_errno > 0) return;
      do
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    else
    {
      if (comma_seen == false) break;
      hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (keyword == TOKEN_KEYWORD_TABLE)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) comma_seen= true;
      else comma_seen= false;
    }
  }
}

void MainWindow::hparse_f_partition_options()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
    if (hparse_errno > 0) return;
    hparse_f_partition_or_subpartition(TOKEN_KEYWORD_PARTITION);
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITIONS") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUBPARTITION") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return;
      hparse_f_partition_or_subpartition(0);
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUBPARTITIONS") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
    {
      do
      {
        hparse_f_partition_or_subpartition_definition(TOKEN_KEYWORD_PARTITION);
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
  }
}

void MainWindow::hparse_f_partition_or_subpartition(int keyword)
{
  bool linear_seen= false;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LINEAR") == 1) linear_seen= true;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HASH") == 1)
  {
    hparse_f_parenthesized_expression();
     if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALGORITHM") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL_WITH_DIGIT, "1") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL_WITH_DIGIT, "2") == 1) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    hparse_f_column_list(1, 0);
    if (hparse_errno > 0) return;
  }
  else if (((linear_seen == false) && (keyword == TOKEN_KEYWORD_PARTITION))
        && ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RANGE") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIST") == 1)))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMNS") == 1)
    {
       hparse_f_column_list(1, 0);
       if (hparse_errno > 0) return;
    }
    else
    {
       hparse_f_parenthesized_expression();
       if (hparse_errno > 0) return;
    }
  }
  else hparse_f_error();
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_partition_or_subpartition_definition(int keyword)
{
  if (keyword == TOKEN_KEYWORD_PARTITION)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PARTITION, TOKEN_TYPE_IDENTIFIER, "[identifier]");
  }
  else
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUBPARTITION");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_SUBPARTITION, TOKEN_TYPE_IDENTIFIER, "[identifier]");
  }
  if (hparse_errno > 0) return;
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUES") == 1))
  {
    /* Todo: LESS THAN only for RANGE; IN only for LIST. Right? */
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LESS") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "THAN");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAXVALUE") == 1) {;}
      else
      {
        /* todo: supposedly this can be either expression or value-list. we take expression-list. */
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
        if (hparse_errno > 0) return;
        do
        {
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAXVALUE") == 1) {;}
          else hparse_f_opr_1(0);
          if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1)
    {
      hparse_f_parenthesized_value_list();
      if (hparse_errno > 0) return;
    }
    hparse_f_table_or_partition_options(TOKEN_KEYWORD_PARTITION);
    if (hparse_errno > 0) return;
    if ((keyword == TOKEN_KEYWORD_PARTITION) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1))
    {
      hparse_f_partition_or_subpartition_definition(0);
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
  }
}

int MainWindow::hparse_f_partition_list(bool is_parenthesized, bool is_maybe_all)
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITION") == 1)
  {
    if (is_parenthesized)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
      if (hparse_errno > 0) return 0;
    }
    if ((is_maybe_all) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1)) {;}
    else
    {
      do
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PARTITION,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return 0;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    if (is_parenthesized)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
    }
    return 1;
  }
  return 0;
}

/* "ALGORITHM" seen, which must mean we're in ALTER VIEW or CREATE VIEW */
void MainWindow::hparse_f_algorithm()
{
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
  if (hparse_errno > 0) return;
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNDEFINED") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MERGE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEMPTABLE") == 1))
     {;}
  else hparse_f_error();
}

/* "SQL" seen, which must mean we're in ALTER VIEW or CREATE VIEW */
void MainWindow::hparse_f_sql()
{
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SECURITY");
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFINER") == 1) {;}
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INVOKER") == 1) {;}
  else hparse_f_error();
}

void MainWindow::hparse_f_for_channel()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR"))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHANNEL");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CHANNEL,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_interval_quantity(int interval_or_event)
{
  if (((interval_or_event == TOKEN_KEYWORD_INTERVAL) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MICROSECOND") == 1))
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SECOND") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MINUTE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOUR") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DAY") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WEEK") == 1)
   || ((interval_or_event == TOKEN_KEYWORD_INTERVAL) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MONTH") == 1))
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUARTER") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "YEAR") == 1)
   || ((interval_or_event == TOKEN_KEYWORD_INTERVAL) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SECOND_MICROSECOND") == 1))
   || ((interval_or_event == TOKEN_KEYWORD_INTERVAL) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MINUTE_MICROSECOND") == 1))
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MINUTE_SECOND") == 1)
   || ((interval_or_event == TOKEN_KEYWORD_INTERVAL) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOUR_MICROSECOND") == 1))
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOUR_SECOND") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOUR_MINUTE") == 1)
   || ((interval_or_event == TOKEN_KEYWORD_INTERVAL) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DAY_MICROSECOND") == 1))
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DAY_SECOND") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DAY_MINUTE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DAY_HOUR") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "YEAR_MONTH") == 1)) {;}
else hparse_f_error();
}

void MainWindow::hparse_f_alter_or_create_event(int statement_type)
{
  if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_EVENT, TOKEN_REFTYPE_EVENT) == 0) hparse_f_error();
  if (hparse_errno > 0) return;

  bool on_seen= false, on_schedule_seen= false;
  if (statement_type == TOKEN_KEYWORD_CREATE)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    on_seen= true;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEDULE");
    if (hparse_errno > 0) return;
    on_schedule_seen= true;
  }
  else /* if statement_type == TOKEN_KEYWORD_ALTER */
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      on_seen= true;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEDULE") == 1)
      {
        on_schedule_seen= true;
      }
    }
  }
  if (on_schedule_seen == true)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AT") == 1)
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVERY") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
      hparse_f_interval_quantity(TOKEN_KEYWORD_EVENT);
      if (hparse_errno > 0) return;
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STARTS") == 1)
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENDS") == 1)
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    }
    on_seen= on_schedule_seen= false;
  }
  if (on_seen == false)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1) on_seen= true;
  }
  if (on_seen == true)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMPLETION");
    if (hparse_errno > 0) return;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT");
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRESERVE");
    if (hparse_errno > 0) return;
  }
  if (statement_type == TOKEN_KEYWORD_ALTER)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RENAME") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_EVENT, TOKEN_REFTYPE_EVENT) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENABLE") == 1) {;}
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISABLE") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVE");
      if (hparse_errno > 0) return;
    }
  }
  hparse_f_comment();
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DO") == 1)
  {
    hparse_f_block(TOKEN_KEYWORD_EVENT, hparse_i);
    if (hparse_errno > 0) return;
  }
  else if (statement_type == TOKEN_KEYWORD_CREATE) hparse_f_error();
}

void MainWindow::hparse_f_create_database()
{
  hparse_f_if_not_exists();
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
  if (hparse_errno > 0) return;
  bool character_seen= false, collate_seen= false;
  for (int i=0; i < 2; ++i)
  {
    bool default_seen= false;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1) default_seen= true;
    if ((character_seen == false) && (hparse_f_character_set() == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_character_set_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      character_seen= true;
    }
    else if (hparse_errno > 0) return;
    else if ((collate_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE") == 1))
    {
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_f_collation_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      collate_seen= true;
    }
    else
    {
      if (default_seen == true) hparse_f_error();
    }
  }
}

/* (index_col_name,...) [index_option] for both CREATE INDEX and CREATE TABLE */
void MainWindow::hparse_f_index_columns(int index_or_table, bool fulltext_seen, bool foreign_seen)
{
  if ((fulltext_seen == false) && (foreign_seen == false))
  {
    /* index_type */
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BTREE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HASH") == 1) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
  }
  if (index_or_table == TOKEN_KEYWORD_INDEX)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");    /* ON tbl_name */
    if (hparse_errno > 0) return;
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  do                                                             /* index_col_name, ... */
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASC") != 1) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DESC");
  } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;

  if (foreign_seen == true)
  {
    hparse_f_reference_definition();
    if (hparse_errno > 0) return;
  }
  else /* if (foreign_seen == false) */
  {
    /* MySQL doesn't check whether these clauses are repeated, but we do. */
    bool key_seen= false, using_seen= false, comment_seen= false, with_seen= false;
    for (;;)                                                             /* index_options */
    {
      if ((key_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY_BLOCK_SIZE") == 1))
      {
        key_seen= true;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1) {;}
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        continue;
      }
      if ((using_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING") == 1))
      {
        using_seen= true;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BTREE") == 1) {;}
        else hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HASH");
        if (hparse_errno > 0) return;
        continue;
      }
      if ((with_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1))
      {
        with_seen= true;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARSER");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PARSER,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
        continue;
      }
      if ((comment_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMENT") == 1))
      {
        comment_seen= true;
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        continue;
      }
      break;
    }
  }
}

void MainWindow::hparse_f_alter_or_create_server(int statement_type)
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_SERVER,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_SERVER,TOKEN_TYPE_LITERAL, "[literal]");
  if (hparse_errno > 0) return;
  if (statement_type == TOKEN_KEYWORD_CREATE)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FOREIGN, "FOREIGN");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATA");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WRAPPER");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_WRAPPER,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_WRAPPER,TOKEN_TYPE_LITERAL, "[literal]");
  }
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTIONS");
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  do
  {
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOST") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SOCKET") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OWNER") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PORT") == 1))
    {
      if (hparse_f_literal() == 0) hparse_f_error();
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
}

/*
  REQUIRE tsl_option is allowed in GRANT, and in CREATE USER (+ALTER?) after MySQL 5.7.6 | MariaDB 10.2.
  WITH resource_option is allowed in GRANT, and in CREATE USER (+ALTER?) after MySQL 5.7.6 | MariaDB 10.2.
  password_option and lock_option are allowed in CREATE or ALTER after MySQL 5.7.6
*/
void MainWindow::hparse_f_require(int who_is_calling, bool proxy_seen, bool role_name_seen)
{
  if ((who_is_calling == TOKEN_KEYWORD_GRANT)
   || ((hparse_dbms_mask & FLAG_VERSION_MARIADB_10_2_2) != 0)
   || ((hparse_dbms_mask & FLAG_VERSION_MYSQL_5_7) != 0))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REQUIRE") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NONE") == 1) {;}
      else
      {
        bool and_seen= false;
        for (;;)
        {
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SSL") == 1) {;}
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "X509") == 1) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CIPHER") == 1) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ISSUER") == 1) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUBJECT") == 1) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          else
          {
            if (and_seen == true) hparse_f_error();
            if (hparse_errno > 0) return;
            break;
          }
          and_seen= false;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AND") == 1) and_seen= true;
        }
      }
    }
  }

  if ((who_is_calling == TOKEN_KEYWORD_GRANT)
   || ((hparse_dbms_mask & FLAG_VERSION_MARIADB_10_2_2) != 0)
   || ((hparse_dbms_mask & FLAG_VERSION_MYSQL_5_7) != 0))
  {
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
     || (((hparse_dbms_mask & FLAG_VERSION_MARIADB_10_2_2) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIA") == 1)))
    {
      for (;;)
      {
        if ((who_is_calling == TOKEN_KEYWORD_GRANT)
         && (role_name_seen == false)
         && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GRANT") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTION");
          if (hparse_errno > 0) return;
        }
        else if ((who_is_calling == TOKEN_KEYWORD_GRANT)
         && (role_name_seen == true)
         && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ADMIN") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTION");
          if (hparse_errno > 0) return;
        }
        else if (proxy_seen == true) {;}
        else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAX_QUERIES_PER_HOUR") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAX_UPDATES_PER_HOUR") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAX_CONNECTIONS_PER_HOUR") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MAX_USER_CONNECTIONS") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
        }
        else break;
      }
    }
  }

  if (((who_is_calling == TOKEN_KEYWORD_CREATE) || (who_is_calling == TOKEN_KEYWORD_ALTER))
   && ((hparse_dbms_mask & FLAG_VERSION_MYSQL_5_7) != 0))
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXPIRE");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NEVER") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTERVAL") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DAY");
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ACCOUNT") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCK") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNLOCK") == 1) {;}
    }
  }
}

void MainWindow::hparse_f_user_specification_list()
{
  do
  {
    if (hparse_f_user_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IDENTIFIED") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY") == 1)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1)
        {
          if (hparse_f_literal() == 0) hparse_f_error();
          if (hparse_errno > 0) return;
        }
        else
        {
          if (hparse_f_literal() == 0) hparse_f_error();
          if (hparse_errno > 0) return;
        }
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PLUGIN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1)
        {
          if (hparse_f_literal() == 0) hparse_f_error();
          if (hparse_errno > 0) return;
        }
      }
    }
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
}

void MainWindow::hparse_f_alter_or_create_view()
{
  if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_VIEW, TOKEN_REFTYPE_VIEW) == 0) hparse_f_error();
  if (hparse_errno > 0) return;
  hparse_f_column_list(0, 0);
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS");
  if (hparse_errno > 0) return;
  if (hparse_f_select(false) == 0) hparse_f_error();
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CASCADED") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL") == 1)) {;}
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECK");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTION");
    if (hparse_errno > 0) return;
  }
}

/* For CALL statement or for PROCEDURE clause in SELECT */
void MainWindow::hparse_f_call()
{
  if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0) hparse_f_error();
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")") == 1) return;
    do
    {
      hparse_f_opr_1(0);
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_commit_or_rollback()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AND") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO") == 1) {;}
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHAIN");
    if (hparse_errno > 0) return;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELEASE");
    if (hparse_errno > 0) return;
  }
  else
  {
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELEASE") == 1) {;}
  }
  if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRANSACTION") == 1)
  {
    hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_TRANSACTION,TOKEN_TYPE_IDENTIFIER, "[identifier]");
  }
}

void MainWindow::hparse_f_explain_or_describe(int block_top)
{
  bool explain_type_seen= false;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXTENDED") == 1)
  {
    explain_type_seen= true;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PARTITIONS") == 1)
  {
    explain_type_seen= true;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FORMAT") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRADITIONAL") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "JSON");
    if (hparse_errno > 0) return;
    explain_type_seen= true;
  }
  if (explain_type_seen == false)
  {
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 1)
    {
      /* DESC table_name wild ... wild can contain '%' and be unquoted. Ugly. */
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) return;
      for (;;)
      {
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, ".") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "%") == 1)) continue;
        break;
      }
      return;
    }
  }
  if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONNNECTION");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
    return;
  }
  hparse_f_explainable_statement(block_top);
  if (hparse_errno > 0) return;
}

/*
   With GRANT|REVOKE, first we check for identifiers (which could be role names)
   (only MariaDB) and if they're there then everything must be role names,
   if they're not there then everything must not be role names.
   Todo: I'm unsure about GRANT|REVOKE PROXY
   is_maybe_all is for check of REVOKE_ALL_PRIVILEGES,GRANT OPTION

   We need lookahead here to check for GRANT token TO|ON, but if token is
   a role we don't need to worry about GRANT role [, role...] -- see
   https://jira.mariadb.org/browse/MDEV-5772. Affected non-reserved words are:
   event, execute, file, proxy, reload, replication, shutdown, super.
*/
void MainWindow::hparse_f_grant_or_revoke(int who_is_calling, bool *role_name_seen)
{
  *role_name_seen= false;
  bool next_must_be_id= false;
  if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
  {
    hparse_f_next_nexttoken();
    if ((hparse_next_token.toUpper() == "TO") && (who_is_calling == TOKEN_KEYWORD_GRANT))
    {
      next_must_be_id= true;
    }
    else if ((hparse_next_token.toUpper() == "FROM") && (who_is_calling == TOKEN_KEYWORD_REVOKE))
    {
      next_must_be_id= true;
    }
  }
  bool is_maybe_all= false;
  int count_of_grants= 0;
  do
  {
    int priv_type= 0;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1)
    {
      /* todo: find out why we're not setting priv_type here */
      is_maybe_all= true;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRIVILEGES") == 1) {;}
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALTER") == 1)
    {
      priv_type= TOKEN_KEYWORD_ALTER;
      is_maybe_all= false;
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROUTINE");
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CREATE") == 1)
    {
      priv_type= TOKEN_KEYWORD_CREATE;
      is_maybe_all= false;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROUTINE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEMPORARY") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW") == 1) {;}
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELETE") == 1)
    {
      priv_type= TOKEN_KEYWORD_DELETE;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DROP") == 1)
    {
      priv_type= TOKEN_KEYWORD_DROP;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENT") == 1))
    {
      priv_type= TOKEN_KEYWORD_EVENT;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXECUTE") == 1))
    {
      priv_type= TOKEN_KEYWORD_EXECUTE;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FILE") == 1))
    {
      priv_type= TOKEN_KEYWORD_FILE;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GRANT") == 1)
    {
      priv_type= TOKEN_KEYWORD_GRANT;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTION");
      if (hparse_errno > 0) return;
      if ((is_maybe_all == true) && (who_is_calling == TOKEN_KEYWORD_REVOKE)) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1)
    {
      priv_type= TOKEN_KEYWORD_INDEX;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INSERT") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      priv_type= TOKEN_KEYWORD_INSERT;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCK") == 1)
    {
      priv_type= TOKEN_KEYWORD_LOCK;
      is_maybe_all= false;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCESS") == 1)
    {
      priv_type= TOKEN_KEYWORD_PROCESS;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROXY") == 1))
    {
      priv_type= TOKEN_KEYWORD_PROXY;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REFERENCES") == 1)
    {
      priv_type= TOKEN_KEYWORD_REFERENCES;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELOAD") == 1))
    {
      priv_type= TOKEN_KEYWORD_RELOAD;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATION") == 1))
    {
      priv_type= TOKEN_KEYWORD_REPLICATION;
      is_maybe_all= false;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CLIENT") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVE") == 1) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SELECT") == 1)
    {
      priv_type= TOKEN_KEYWORD_SELECT;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SHOW") == 1)
    {
      priv_type= TOKEN_KEYWORD_SHOW;
      is_maybe_all= false;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASES") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW") == 1) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SHUTDOWN") == 1))
    {
      priv_type= TOKEN_KEYWORD_SHUTDOWN;
      is_maybe_all= false;
    }
    else if ((next_must_be_id == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUPER") == 1))
    {
      priv_type= TOKEN_KEYWORD_SUPER;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRIGGER") == 1)
    {
      priv_type= TOKEN_KEYWORD_TRIGGER;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE") == 1)
    {
      priv_type= TOKEN_KEYWORD_UPDATE;
      is_maybe_all= false;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USAGE") == 1)
    {
      priv_type= TOKEN_KEYWORD_USAGE;
      is_maybe_all= false;
    }
    else
    {
      if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
       && (hparse_next_token.toUpper() != "ON")
       && (hparse_next_token != ",")
       && (hparse_next_token != "(")
       && (count_of_grants == 0)
       && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ROLE,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)) /* possible role name? */
      {
        *role_name_seen= true;
        return;
      }
      hparse_f_error();
      is_maybe_all= false;
    }
    if (hparse_errno > 0) return;
    ++count_of_grants;
    if ((priv_type == TOKEN_KEYWORD_SELECT)
     || (priv_type == TOKEN_KEYWORD_INSERT)
     || (priv_type == TOKEN_KEYWORD_UPDATE))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "("))
      {
        do
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
          if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
    }
  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));

  hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");
  if (hparse_errno > 0) return;
  main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCEDURE") == 1)
  {
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0)
      hparse_f_error();
    return;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FUNCTION") == 1)
  {
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION, TOKEN_REFTYPE_FUNCTION) == 0)
      hparse_f_error();
    return;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 1)
    {;}
  if (hparse_f_qualified_name_of_object_with_star(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0)
    hparse_f_error();
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_insert_or_replace()
{
  if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OR") == 1)
  {
    hparse_f_conflict_algorithm();
    if (hparse_errno > 0) return;
  }
  if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
  {
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
    if (hparse_errno > 0) return;
  }
  else
    hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
  if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
  if (hparse_errno > 0) return;
  hparse_f_partition_list(true, false);
  if (hparse_errno > 0) return;
  bool col_name_list_seen= false;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    do
    {
      if (hparse_f_qualified_name_of_operand(false) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    col_name_list_seen= true;
  }
  if ((col_name_list_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_assignment(TOKEN_KEYWORD_INSERT);
    if (hparse_errno > 0) return;
  }
  else if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUES") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUE")) == 1)
  {
    /* 2017-04-30: "VALUES ()" is legal */
    for (;;)
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
      if (hparse_errno > 0) return;
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR,")") == 0)
      {
        hparse_f_expression_list(TOKEN_KEYWORD_INSERT);
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 0) break;
    }
  }
  else if (hparse_f_select(false) == 1)
  {
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DEFAULT, "DEFAULT") == 1)
  {
    hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_VALUES, "VALUES");
    if (hparse_errno > 0) return;
  }
  else hparse_f_error();
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_conflict_clause()
{
  if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONFLICT");
    if (hparse_errno > 0) return;
    hparse_f_conflict_algorithm();
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_conflict_algorithm()
{
  if ((hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROLLBACK") == 1)
   || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ABORT") == 1)
   || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FAIL") == 1)
   || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1)
   || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLACE") == 1))
  {
    return;
  }
  hparse_f_error();
}

void MainWindow::hparse_f_condition_information_item_name()
{
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CLASS_ORIGIN") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUBCLASS_ORIGIN") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RETURNED_SQLSTATE") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MESSAGE_TEXT") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MYSQL_ERRNO") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONSTRAINT_CATALOG") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONSTRAINT_SCHEMA") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONSTRAINT_NAME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CATALOG_NAME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEMA_NAME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE_NAME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMN_NAME") == 1)
   || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURSOR_NAME") == 1)) {;}
  else hparse_f_error();
}

int MainWindow::hparse_f_signal_or_resignal(int who_is_calling, int block_top)
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQLSTATE") == 1)
  {
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUE");
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
  }
  else if (hparse_f_conditions(block_top) == 1) {;}
  else if (who_is_calling == TOKEN_KEYWORD_SIGNAL) return 0;
  if (hparse_errno > 0) return 0;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    do
    {
      hparse_f_condition_information_item_name();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_errno > 0) return 0;
      if (hparse_f_literal() == 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      }
      if (hparse_errno > 0) return 0;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  return 1;
}

/* An INTO clause may appear in two different places within a SELECT. */
int MainWindow::hparse_f_into()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO"))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OUTFILE") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      /* CHARACTER SET character-set-name and export_options */
      hparse_f_infile_or_outfile();
      if (hparse_errno > 0) return 0;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DUMPFILE") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    else
    {
      do
      {
        if (hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
        if (hparse_errno > 0) return 0;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    return 1;
  }
  return 0;
}

/*
  Todo: A problem with WITH is that it makes it hard to know what the true
  statement type is. Perhaps we should change main_token_flags?
*/
void MainWindow::hparse_f_with_clause(int block_top)
{
  hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RECURSIVE");
  hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_WITH_TABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS");
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
  if (hparse_errno > 0) return;
  hparse_f_select(false);
  if (hparse_errno > 0) return;
  hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
  if (hparse_errno > 0) return;
  if (hparse_f_is_special_verb(TOKEN_KEYWORD_WITH) == false) return;
  hparse_f_statement(block_top);
  return;
}

/*
  "SELECT ..." or "(SELECT ...)"
*/
int MainWindow::hparse_f_select(bool select_is_already_eaten)
{
  if ((hparse_statement_type == 0)
   || (hparse_statement_type == TOKEN_KEYWORD_WITH))
  {
    hparse_statement_type= TOKEN_KEYWORD_SELECT;
  }
  if (hparse_subquery_is_allowed == false) hparse_subquery_is_allowed= true;
  if (select_is_already_eaten == false)
  {
    /* (SELECT is the only statement that can be in parentheses, eh?) */
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
    {
      if (hparse_f_select(false) == 0)
      {
        hparse_f_error();
        return 0;
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNION") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISTINCT") == 1)) {;}
        int return_value= hparse_f_select(false);
        if (hparse_errno > 0) return 0;
        if (return_value == 0)
        {
          hparse_f_error();
          return 0;
        }
      }
      hparse_f_order_by(0);
      if (hparse_errno > 0) return 0;
      hparse_f_limit(TOKEN_KEYWORD_SELECT);
      if (hparse_errno > 0) return 0;
      return 1;
    }
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SELECT") == 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  for (;;)
  {
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISTINCT") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISTINCTROW") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HIGH_PRIORITY") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STRAIGHT_JOIN") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_SMALL_RESULT") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_BIG_RESULT") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_BUFFER_RESULT") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_CACHE") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_NO_CACHE") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_CALC_FOUND_ROWS") == 1))
    {
      ;
    }
    else break;
  }
  hparse_f_expression_list(TOKEN_KEYWORD_SELECT);
  if (hparse_errno > 0) return 0;
  hparse_f_into();
  if (hparse_errno > 0) return 0;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1)         /* FROM + some subsequent clauses are optional */
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    /* DUAL is a reserved word, perhaps the only one that could ever be an identifier */
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLE,TOKEN_TYPE_KEYWORD, "DUAL") != 1)
    {
      if (hparse_f_table_references() == 0) hparse_f_error();
    }
    if (hparse_errno > 0) return 0;
    hparse_f_where();
    if (hparse_errno > 0) return 0;
    bool is_group_by_seen= false;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GROUP"))
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return 0;
      is_group_by_seen= true;
      do
      {
        hparse_f_opr_1(0);
        if (hparse_errno > 0) return 0;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASC") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DESC") == 1)) {;}
      } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROLLUP");
        if (hparse_errno > 0) return 0;
      }
    }
    if ((is_group_by_seen == true) || ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) == 0))
    {
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HAVING"))
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        hparse_f_opr_1(0);
        if (hparse_errno > 0) return 0;
      }
    }
  }
  hparse_f_order_by(TOKEN_KEYWORD_SELECT);
  if (hparse_errno > 0) return 0;
  hparse_f_limit(TOKEN_KEYWORD_SELECT);
  if (hparse_errno > 0) return 0;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCEDURE"))
  {
    hparse_f_call();
    if (hparse_errno > 0) return 0;
  }
  hparse_f_into();
  if (hparse_errno > 0) return 0;
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE");
    if (hparse_errno > 0) return 0;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCK") == 1)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SHARE");
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MODE");
    if (hparse_errno > 0) return 0;
  }
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNION") == 1)
  {
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DISTINCT") == 1)) {;}
    if (hparse_f_select(false) == 0)
    {
      hparse_f_error();
      return 0;
    }
  }
  if ((hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTERSECT") == 1)
   || (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXCEPT") == 1))
  {
    if (hparse_f_select(false) == 0)
    {
      hparse_f_error();
      return 0;
    }
  }
  return 1;
}

void MainWindow::hparse_f_where()
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHERE"))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_opr_1(0);
  }
  if (hparse_errno > 0) return;
}

int MainWindow::hparse_f_order_by(int who_is_calling)
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ORDER") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
    if (hparse_errno > 0) return 0;
    do
    {
      hparse_f_opr_1(who_is_calling);
      if (hparse_errno > 0) return 0;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ASC") == 0) hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DESC");
    } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    return 1;
  }
  else return 0;
}

/* LIMIT 1 or LIMIT 1,0 or LIMIT 1 OFFSET 0 from SELECT, DELETE, UPDATE, or SHOW */
void MainWindow::hparse_f_limit(int who_is_calling)
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIMIT") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 0)
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    if ((who_is_calling == TOKEN_KEYWORD_DELETE) || (who_is_calling == TOKEN_KEYWORD_UPDATE)) return;
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OFFSET") == 1))
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 0)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
    }
  }
}

void MainWindow::hparse_f_like_or_where()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIKE") == 1)
  {
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHERE") == 1)
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_from_or_like_or_where()
{
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  hparse_f_like_or_where();
}

/* SELECT ... INTO OUTFILE and LOAD DATA INFILE have a similar clause. */
void MainWindow::hparse_f_infile_or_outfile()
{
  if (hparse_f_character_set() == 1)
  {
    if (hparse_f_character_set_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  if (hparse_errno > 0) return;

  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIELDS") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMNS") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TERMINATED") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    bool enclosed_seen= false;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTIONALLY") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENCLOSED");
      if (hparse_errno > 0) return;
      enclosed_seen= true;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENCLOSED") == 1)
    {
      enclosed_seen= true;
    }
    if (enclosed_seen == true)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ESCAPED") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LINES") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STARTING") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TERMINATED") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
  }
}

void MainWindow::hparse_f_show_columns()
{
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 0)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
  }
  if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
  if (hparse_errno > 0) return;
  hparse_f_from_or_like_or_where();
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_indexes_or_keys() /* for SHOW {INDEX | INDEXES | KEYS} */
{
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHERE") == 1)
  {
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
  }
}

/*
   For CREATE/ALTER: some clauses precede the object type, so e.g. we don't know yet
   whether it's a table, a view, an index, or whatever.
   We'll take such clauses in any order, but won't allow duplicates or impossibles.
   We'll return hparse_flags, which determines what can follow -- e.g. after CREATE UNIQUE
   we won't expect TABLE.
   schema=database, function+procedure+trigger+event=routine
*/
#define HPARSE_FLAG_DATABASE   1
#define HPARSE_FLAG_ROUTINE    2
#define HPARSE_FLAG_INDEX      8
#define HPARSE_FLAG_SERVER     32
#define HPARSE_FLAG_TABLE      64
#define HPARSE_FLAG_TABLESPACE 128
#define HPARSE_FLAG_TRIGGER    512
#define HPARSE_FLAG_USER       1024
#define HPARSE_FLAG_VIEW       2048
#define HPARSE_FLAG_INSTANCE   4096
#define HPARSE_FLAG_ANY        65535

void MainWindow::hparse_f_alter_or_create_clause(int who_is_calling, unsigned short int *hparse_flags, bool *fulltext_seen)
{
  bool algorithm_seen= false, definer_seen= false, sql_seen= false, temporary_seen= false;
  bool unique_seen= false, or_seen= false, ignore_seen= false, online_seen= false;
  bool aggregate_seen= false;
  *fulltext_seen= false;
  (*hparse_flags)= HPARSE_FLAG_ANY;

  /* in MySQL OR REPLACE is only for views, in MariaDB OR REPLACE is for all creates */
  int or_replace_flags;
  if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) or_replace_flags= HPARSE_FLAG_ANY;
  else or_replace_flags= HPARSE_FLAG_VIEW;

  if (who_is_calling == TOKEN_KEYWORD_CREATE)
  {
    ignore_seen= true;
    if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0) online_seen= true;
  }
  else
  {
    temporary_seen= true; (*fulltext_seen)= true, unique_seen= true, or_seen= true;
    aggregate_seen= true;
  }
  for (;;)
  {
    if ((((*hparse_flags) & or_replace_flags) != 0) && (or_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OR") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLACE");
      if (hparse_errno > 0) return;
      or_seen= true; (*hparse_flags) &= or_replace_flags;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_VIEW) != 0) && (algorithm_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALGORITHM") == 1))
    {
      hparse_f_algorithm();
      if (hparse_errno > 0) return;
      algorithm_seen= true; (*hparse_flags) &= HPARSE_FLAG_VIEW;
    }
    else if ((((*hparse_flags) & (HPARSE_FLAG_VIEW + HPARSE_FLAG_ROUTINE)) != 0) && (definer_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFINER") == 1))
    {
      hparse_f_definer();
      if (hparse_errno > 0) return;
      definer_seen= true; (*hparse_flags) &= (HPARSE_FLAG_VIEW + HPARSE_FLAG_ROUTINE);
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_VIEW) != 0) && (sql_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL") == 1))
    {
      hparse_f_sql();
      if (hparse_errno > 0) return;
      sql_seen= true;  (*hparse_flags) &= HPARSE_FLAG_VIEW;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_TABLE) != 0) && (ignore_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1))
    {
      ignore_seen= true; (*hparse_flags) &= HPARSE_FLAG_TABLE;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_TABLE) != 0) && (temporary_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEMPORARY") == 1))
    {
      temporary_seen= true; (*hparse_flags) &= HPARSE_FLAG_TABLE;
    }
    else if ((((*hparse_flags) & (HPARSE_FLAG_TABLE | HPARSE_FLAG_TRIGGER | HPARSE_FLAG_VIEW)) != 0) && (temporary_seen == false) && (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEMP") == 1))
    {
      temporary_seen= true; (*hparse_flags) &= (HPARSE_FLAG_TABLE | HPARSE_FLAG_TRIGGER | HPARSE_FLAG_VIEW | HPARSE_FLAG_ROUTINE);
    }
    else if ((((*hparse_flags) & (HPARSE_FLAG_TABLE | HPARSE_FLAG_TRIGGER | HPARSE_FLAG_VIEW)) != 0) && (temporary_seen == false) && (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEMPORARY") == 1))
    {
      temporary_seen= true; (*hparse_flags) &= (HPARSE_FLAG_TABLE | HPARSE_FLAG_TRIGGER | HPARSE_FLAG_VIEW | HPARSE_FLAG_ROUTINE);
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_TABLE) != 0) && ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ONLINE") == 1))
    {
      online_seen= true; (*hparse_flags) &= (HPARSE_FLAG_INDEX | HPARSE_FLAG_TABLE);
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_TABLE) != 0) && ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OFFLINE") == 1))
    {
      online_seen= true; (*hparse_flags) &= HPARSE_FLAG_INDEX;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_INDEX) != 0) && (unique_seen == false) && ((*fulltext_seen) == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FULLTEXT") == 1))
    {
      (*fulltext_seen)= true; (*hparse_flags) &= HPARSE_FLAG_INDEX;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_INDEX) != 0) && ((*fulltext_seen) == false) && (unique_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SPATIAL") == 1))
    {
      (*fulltext_seen)= true; (*hparse_flags) &= HPARSE_FLAG_INDEX;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_INDEX) != 0) && (unique_seen == false) && ((*fulltext_seen) == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNIQUE") == 1))
    {
      unique_seen= true; (*hparse_flags) &= HPARSE_FLAG_INDEX;
    }
    else if ((((*hparse_flags) & HPARSE_FLAG_ROUTINE) != 0) && (aggregate_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AGGREGATE") == 1))
    {
      aggregate_seen= true; (*hparse_flags) &= HPARSE_FLAG_ROUTINE;
    }
    else break;
  }
}

/* ; or (; + delimiter) or delimiter or \G or \g */
int MainWindow::hparse_f_semicolon_and_or_delimiter(int calling_statement_type)
{
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_DELIMITER, "\\G") == 1)
  {
    return 1;
  }
  /* TEST!! removed next line */
  if ((calling_statement_type == 0) || (calling_statement_type != 0))
  {
    if (hparse_f_accept(FLAG_VERSION_ALL_OR_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";") == 1)
    {
      hparse_f_accept(FLAG_VERSION_ALL_OR_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_DELIMITER, hparse_delimiter_str);
      return 1;
    }
    else if (hparse_f_accept(FLAG_VERSION_ALL_OR_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_DELIMITER, hparse_delimiter_str) == 1) return 1;
    return 0;
  }
  else return (hparse_f_accept(FLAG_VERSION_ALL_OR_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";"));
}

/*
  For EXPLAIN and perhaps for ANALYZE, we want to accept only a
  statement that would be legal therein. So check if that's what follows,
  if it is then call hparse_f_statement, if it's not then call
  hparse_f_accept which is guaranteed to fail.
  Return 1 if it was a statement, else return 0 (which might also mean error).
*/
int MainWindow::hparse_f_explainable_statement(int block_top)
{
  QString hparse_token_upper= hparse_token.toUpper();
  if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
  {
    if ((hparse_token_upper == "DELETE")
     || (hparse_token_upper == "INSERT")
     || (hparse_token_upper == "REPLACE")
     || (hparse_token_upper == "SELECT")
     || (hparse_token_upper == "UPDATE"))
    {
      hparse_f_statement(block_top);
      if (hparse_errno > 0) return 0;
      return 1;
    }
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELETE");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INSERT");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLACE");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SELECT");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE");
    return 0;
  }
  else if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
  {
    if ((hparse_token_upper == "DELETE")
     || (hparse_token_upper == "SELECT")
     || (hparse_token_upper == "UPDATE"))
    {
      hparse_f_statement(block_top);
      if (hparse_errno > 0) return 0;
      return 1;
    }
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELETE");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SELECT");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE");
    return 0;
  }
  else if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
  {
    hparse_f_statement(block_top);
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}

/* TODO: I THINK I'M FORGETTING TO SAY return FOR A LOT OF MAIN STATEMENTS! */
/*
statement =
    "connect" "create" "drop" etc. etc.
    The idea is to parse everything that's described in the MySQL 5.7 manual.
    Additionally, depending on flags, parse MariaDB or Tarantool statements.
*/
void MainWindow::hparse_f_statement(int block_top)
{
  if (hparse_errno > 0) return;
  hparse_statement_type= 0;
  hparse_subquery_is_allowed= 0;

  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALTER"))
  {
    hparse_statement_type= TOKEN_KEYWORD_ALTER;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    unsigned short int hparse_flags; bool fulltext_seen;
    hparse_f_alter_or_create_clause(TOKEN_KEYWORD_ALTER, &hparse_flags, &fulltext_seen);
    if ((((hparse_flags) & HPARSE_FLAG_DATABASE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1))
    {
      hparse_f_alter_database();
    }
    else if ((((hparse_flags) & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENT") == 1))
    {
      hparse_f_alter_or_create_event(TOKEN_KEYWORD_ALTER);
      if (hparse_errno > 0) return;
    }
    else if ((((hparse_flags) & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FUNCTION") == 1))
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION, TOKEN_REFTYPE_FUNCTION) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      hparse_f_characteristics();
    }
    else if ((((hparse_flags) & HPARSE_FLAG_INSTANCE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INSTANCE") == 1))
    {
      /* Todo: This statement appears to have disappeared. */
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROTATE");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INNODB");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
      if (hparse_errno > 0) return;
    }
    /* TODO: ALTER LOGFILE GROUP is not supported */
    else if ((((hparse_flags) & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCEDURE") == 1))
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      hparse_f_characteristics();
    }
    else if ((((hparse_flags) & HPARSE_FLAG_DATABASE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEMA") == 1))
    {
      hparse_f_alter_database();
    }
    else if ((((hparse_flags) & HPARSE_FLAG_SERVER) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SERVER, "SERVER") == 1))
    {
      hparse_f_alter_or_create_server(TOKEN_KEYWORD_ALTER);
      if (hparse_errno > 0) return;
    }
    else if ((((hparse_flags) & HPARSE_FLAG_TABLE) != 0) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 1))
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      do
      {
        hparse_f_alter_specification();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      hparse_f_partition_options();
    }
    else if ((((hparse_flags) & HPARSE_FLAG_USER) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER") == 1))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      hparse_f_user_specification_list();
      if (hparse_errno > 0) return;
      hparse_f_require(TOKEN_KEYWORD_ALTER, false, false);
      if (hparse_errno > 0) return;
    }
    else if ((((hparse_flags) & HPARSE_FLAG_VIEW) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW") == 1))
    {
      hparse_f_alter_or_create_view();
      if (hparse_errno > 0) return;
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ANALYZE") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_ANALYZE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    int table_or_view;
    if (hparse_f_analyze_or_optimize(TOKEN_KEYWORD_ANALYZE, &table_or_view) == 1)
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PERSISTENT") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
          if (hparse_errno > 0) return;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMNS");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
          if (hparse_errno > 0) return;
          for (;;)
          {
            if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
            {
              if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) continue;
            }
            break;
          }
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEXES");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
          if (hparse_errno > 0) return;
          for (;;)
          {
            if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1)
            {
              if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) continue;
            }
            break;
          }
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
          if (hparse_errno > 0) return;
        }
      }
      return;
    }
    if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FORMAT") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRADITIONAL") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "JSON");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_explainable_statement(block_top) == 1) return;
      if (hparse_errno > 0) return;
    }
    hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_ATTACH, "ATTACH") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_ATTACH;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1) {;}
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_BEGIN_WORK, "BEGIN") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_BEGIN_WORK; /* don't confuse with BEGIN for compound */
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WORK") == 1) {;}
    if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
    {
      if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFERRED") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IMMEDIATE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXCLUSIVE") == 1) {;}
      if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRANSACTION") == 1) {;}
    }
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINLOG") == 1)
  {
    //hparse_statement_type= TOKEN_KEYWORD_BINLOG;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CACHE") == 1)
  {
    //hparse_statement_type= TOKEN_KEYWORD_CACHE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX");
    if (hparse_errno > 0) return;
    do
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      bool parenthesis_is_seen= false;
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1))
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
        if (hparse_errno > 0) return;
        parenthesis_is_seen= true;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        parenthesis_is_seen= true;
      }
      if (parenthesis_is_seen == true)
      {
        do
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX, TOKEN_TYPE_IDENTIFIER, "[identifier]");
          if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    /* todo: partition clause should not be legal in MariaDB */
    /* todo: I think ALL is within parentheses? */
    if (hparse_f_partition_list(true, true) == 0)
    {
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_KEY_CACHE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CALL") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_CALL;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_call();
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHANGE") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_CHANGE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER") == 1)
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
      if (hparse_errno > 0) return;
      do
      {
        if ((((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DO_DOMAIN_IDS") == 1))
         || (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE_DOMAIN_IDS") == 1))
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE_SERVER_IDS") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
          if (hparse_errno > 0) return;
          do
          {
            hparse_f_literal(); /* this allows "()" */
            if (hparse_errno > 0) return;
          } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_AUTO_POSITION")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_BIND")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_CONNECT_RETRY")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_DELAY"))) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MARIADB_10_2_3, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_DELAY")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_HEARTBEAT_PERIOD")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_HOST")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_LOG_FILE")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_LOG_POS")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_PASSWORD")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_PORT")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_RETRY_COUNT"))) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL"))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_CA")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_CAPATH")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_CERT")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_CIPHER")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_CRL")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_CRLPATH")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_KEY")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_SSL_VERIFY_SERVER_CERT"))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "0") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "1");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_USER")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_USE_GTID"))) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_TLS_VERSION"))) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELAY_LOG_FILE")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELAY_LOG_POS")) {hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "="); hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");}
        else hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      hparse_f_for_channel();
      if (hparse_errno > 0) return;
      return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATION") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FILTER");
      if (hparse_errno > 0) return;
      do
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_DO_DB") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_IGNORE_DB") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_DO_TABLE") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_IGNORE_TABLE") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_WILD_DO_TABLE") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_WILD_IGNORE_TABLE") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLICATE_REWRITE_DB") == 1) {;}
        else hparse_f_error();
        if (hparse_errno > 0) return;
        /* TODO: Following is garbage. We need lists of databases or tables */
        hparse_f_column_list(1, 0); /* Todo: take into account what kind of list it should be */
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECK") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_CHECK;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 1)
    {
      if (hparse_errno > 0) return;
      do
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      for (;;)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPGRADE");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUICK") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FAST") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MEDIUM") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXTENDED") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHANGED") == 1) {;}
        else break;
      }
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW") == 1))
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_VIEW) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CHECKSUM") == 1)
  {
    //hparse_statement_type= TOKEN_KEYWORD_CHECKSUM;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
    if (hparse_errno > 0) return;
    do
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUICK") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXTENDED") == 1)) {;}
    else hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMIT") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_COMMIT;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WORK") == 1) {;}
    hparse_f_commit_or_rollback();
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONNECT"))
  {
    hparse_statement_type= TOKEN_KEYWORD_CONNECT;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CREATE, "CREATE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_CREATE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    unsigned short int hparse_flags;
    bool fulltext_seen;
    hparse_f_alter_or_create_clause(TOKEN_KEYWORD_CREATE, &hparse_flags, &fulltext_seen);
    if (hparse_errno > 0) return;
    if (((hparse_flags & HPARSE_FLAG_DATABASE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1))
    {
      hparse_f_create_database();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_flags & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EVENT, "EVENT") == 1))
    {
      hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      hparse_f_alter_or_create_event(TOKEN_KEYWORD_CREATE);
      if (hparse_errno > 0) return;
    }
    else if (((hparse_flags & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FUNCTION, "FUNCTION") == 1))
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION, TOKEN_REFTYPE_FUNCTION) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      /* If (parameter_list) isn't there, it might be a UDF */
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RETURNS") == 1)
      {
        /* Manual doesn't mention INT or DEC. I wonder what else it doesn't mention. */
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STRING") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTEGER") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INT") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REAL") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DECIMAL") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEC") == 1)) {;}
        else hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SONAME");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        if (hparse_errno > 0) return;
      }
      else
      {
        hparse_f_parameter_list(TOKEN_KEYWORD_FUNCTION);
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RETURNS");
        if (hparse_errno > 0) return;
        if (hparse_f_data_type() == -1) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_characteristics();
        if (hparse_errno > 0) return;
        hparse_f_block(TOKEN_KEYWORD_FUNCTION, hparse_i);
        if (hparse_errno > 0) return;
      }
    }
    else if (((hparse_flags & HPARSE_FLAG_INDEX) != 0) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1))
    {
      if ((hparse_dbms_mask & (FLAG_VERSION_MARIADB_ALL | FLAG_VERSION_TARANTOOL)) != 0) hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_INDEX, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;                                    /* index_name */
      hparse_f_index_columns(TOKEN_KEYWORD_INDEX, fulltext_seen, false);
      if (hparse_errno > 0) return;
      if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
      {
        hparse_f_where();
        if (hparse_errno > 0) return;
      }
      hparse_f_algorithm_or_lock();
    }
    else if (((hparse_flags & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PROCEDURE, "PROCEDURE") == 1))
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      hparse_f_parameter_list(TOKEN_KEYWORD_PROCEDURE);
      if (hparse_errno > 0) return;
      hparse_f_characteristics();
      if (hparse_errno > 0) return;
      hparse_f_block(TOKEN_KEYWORD_PROCEDURE, hparse_i);
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && ((hparse_flags & HPARSE_FLAG_USER) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_ROLE, "ROLE") == 1))
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      if (QString::compare(hparse_token, "NONE", Qt::CaseInsensitive) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ROLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ADMIN");
        if (hparse_errno > 0) return;
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
    }
    else if (((hparse_flags & HPARSE_FLAG_DATABASE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SCHEMA, "SCHEMA") == 1))
    {
      hparse_f_create_database();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_flags & HPARSE_FLAG_SERVER) != 0) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SERVER, "SERVER") == 1))
    {
      hparse_f_alter_or_create_server(TOKEN_KEYWORD_CREATE);
      if (hparse_errno > 0) return;
    }
    else if (((hparse_flags & HPARSE_FLAG_TABLE) != 0) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_TABLE, "TABLE") == 1))
    {
      ; /* TODO: This accepts "CREATE TABLE x;" which has 0 columns */
      hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      bool element_is_seen= false;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIKE") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        return;
      }
      else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SERVER, "SERVER") == 1)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_SERVER,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LUA, "LUA");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        if (hparse_errno > 0) return;
        return;
      }
      else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LIKE") == 1)
        {
          if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
          if (hparse_errno > 0) return;
          return;
        }
        main_token_flags[hparse_i] |= TOKEN_FLAG_IS_START_IN_COLUMN_LIST;
        bool comma_is_seen;
        do
        {
          comma_is_seen= false;
          if (hparse_f_qualified_name_of_operand(false) == 1)
          {
            hparse_f_column_definition();
            if (hparse_errno > 0) return;
          }
          else
          {
            if (hparse_errno > 0) return;
            hparse_f_create_definition();
            if (hparse_errno > 0) return;
          }
          if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","))
          {
            main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_END_IN_COLUMN_LIST;
            comma_is_seen= true;
          }
        } while (comma_is_seen);
        element_is_seen= true;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      if ((element_is_seen == true)
       && ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0))
      {
        if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITHOUT") == 1)
        {
          main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
          hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROWID");
        }
      }
      else
      {
        hparse_f_table_or_partition_options(TOKEN_KEYWORD_TABLE);
        if (hparse_errno > 0) return;
        hparse_f_partition_options();
        if (hparse_errno > 0) return;
        bool ignore_or_as_seen= false;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") || hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLACE") == 1))
        {
          main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
          ignore_or_as_seen= true;
        }
        if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
        {
          hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS");
          if (hparse_errno > 0) return;
          ignore_or_as_seen= true;
        }
        else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1) ignore_or_as_seen= true;
        bool select_is_seen= false;
        if (ignore_or_as_seen == true)
        {
          if (hparse_f_select(false) == 0)
          {
            hparse_f_error();
            return;
          }
          select_is_seen= true;
        }
        else if (hparse_f_select(false) != 0) select_is_seen= true;
        if (hparse_errno > 0) return;
        if ((element_is_seen == false) && (select_is_seen == false))
        {
          hparse_f_error();
          return;
        }
      }
    }
    else if (((hparse_flags & HPARSE_FLAG_TABLESPACE) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_TABLESPACE, "TABLESPACE") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLESPACE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ADD");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATAFILE");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FILE_BLOCK_SIZE") == 1)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1) {;}
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINE") == 1)
      {
        hparse_f_engine();
        if (hparse_errno > 0) return;
      }
    }
    else if (((hparse_flags & HPARSE_FLAG_ROUTINE) != 0) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_TRIGGER, "TRIGGER") == 1))
    {
      if ((hparse_dbms_mask & (FLAG_VERSION_MARIADB_ALL | FLAG_VERSION_TARANTOOL)) != 0) hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TRIGGER, TOKEN_REFTYPE_TRIGGER) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BEFORE") == 1)
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INSTEAD") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OF");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AFTER") == 1)
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      else
      {
        hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INSERT") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELETE") == 1) {;}
      else hparse_f_error();
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EACH");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROW");
        if (hparse_errno > 0) return;
      }
      else
      {
        if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0)
        {
          hparse_f_error();
          if (hparse_errno > 0) return;
        }
      }
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_5_7 | FLAG_VERSION_MARIADB_10_2_3, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOLLOWS") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRECEDES")) == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TRIGGER, TOKEN_REFTYPE_TRIGGER) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
      {
        if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN") == 1)
        {
          hparse_f_opr_1(0);
          if (hparse_errno > 0) return;
        }
        hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_BEGIN, "BEGIN");
        if (hparse_errno > 0) return;
        bool statement_is_seen= false;
        for (;;)
        {
          if (statement_is_seen)
          {
            if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1) break;
          }
          /* check first word of statement is okay, else return error */
          if (hparse_f_is_special_verb(TOKEN_KEYWORD_TRIGGER) == false) return;
          hparse_f_statement(block_top);
          if (hparse_errno > 0) return;
          /* This kludge occurs more than once. */
          if ((hparse_prev_token != ";") && (hparse_prev_token != hparse_delimiter_str))
          {
            if (hparse_f_semicolon_and_or_delimiter(TOKEN_KEYWORD_TRIGGER) == 0) hparse_f_error();
          }
          if (hparse_errno > 0) return;
          statement_is_seen= true;
        }
      }
      else
      {
        hparse_f_block(TOKEN_KEYWORD_TRIGGER, hparse_i);
        if (hparse_errno > 0) return;
      }
    }
    else if (((hparse_flags & HPARSE_FLAG_USER) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_USER, "USER") == 1))
    {
      hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      hparse_f_user_specification_list();
      if (hparse_errno > 0) return;
      hparse_f_require(TOKEN_KEYWORD_CREATE, false, false);
      if (hparse_errno > 0) return;
    }
    else if (((hparse_flags & HPARSE_FLAG_VIEW) != 0) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_VIEW, "VIEW") == 1))
    {
      if ((hparse_dbms_mask & (FLAG_VERSION_MARIADB_ALL | FLAG_VERSION_TARANTOOL)) != 0) hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      hparse_f_alter_or_create_view();
      if (hparse_errno > 0) return;
    }
    else if ((((hparse_flags) & HPARSE_FLAG_TABLE) != 0) && (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIRTUAL") == 1))
    {
      hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
      if (hparse_errno > 0) return;
      hparse_f_if_not_exists();
      if (hparse_errno > 0) return;
      hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE);
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_PLUGIN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        for (;;)
        {
          if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_PARAMETER,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1) {;}
          if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) continue;
          else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")") == 1) break;
          else hparse_f_error();
          if (hparse_errno > 0) return;
        }
      }
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DEALLOCATE, "DEALLOCATE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_DEALLOCATE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PREPARE");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_STATEMENT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DELETE, "DELETE"))
  {
    /* todo: look up how partitions are supposed to be handled */
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_DELETE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOW_PRIORITY")) {;}
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUICK")) {;}
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE")) {;}
    bool is_from_seen= false;
    if ((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM");
      if (hparse_errno > 0) return;
      is_from_seen= true;
    }
    if ((is_from_seen == true)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1))
    {
      bool multi_seen= false;
      if (hparse_f_qualified_name_with_star() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","))
      {
        multi_seen= true;
        do
        {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      }
      if (multi_seen == true) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING");
      if (hparse_errno > 0) return;
      if ((multi_seen == true) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING") == 1))
      {
        /* DELETE ... tbl_name[.*] [, tbl_name[.*]] ... FROM table_references [WHERE ...] */
        if (hparse_f_table_references() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_where();
        if (hparse_errno > 0) return;
        return;
      }
      /* DELETE ... FROM tbl_name [WHERE] [ORDER BY] LIMIT] */
      hparse_f_where();
      if (hparse_errno > 0) return;
      if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0)
      {
        hparse_f_order_by(0);
        if (hparse_errno > 0) return;
        hparse_f_limit(TOKEN_KEYWORD_DELETE);
        if (hparse_errno > 0) return;
      }
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RETURNING") == 1)
        {
          hparse_f_expression_list(TOKEN_KEYWORD_SELECT);
          if (hparse_errno > 0) return;
        }
      }
      return;
    }
    if (hparse_errno > 0) return;
    /* DELETE tbl_name[.*] [, tbl_name[.*]] ... FROM table_references [WHERE ...] */
    do
    {
      if (hparse_f_qualified_name_with_star() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM");
    if (hparse_errno > 0) return;
    if (hparse_f_table_references() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    hparse_f_where();
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DESC, "DESC") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_DESC;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_explain_or_describe(block_top);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DESCRIBE, "DESCRIBE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_DESCRIBE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_explain_or_describe(block_top);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DETACH, "DETACH") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_DETACH;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1) {;}
    hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DO, "DO"))
  {
    hparse_statement_type= TOKEN_KEYWORD_DO;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    do
    {
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DROP, "DROP"))         /* drop database/event/etc. */
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_DROP;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    bool temporary_seen= false, online_seen= false;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TEMPORARY") == 1) temporary_seen= true;
    if ((temporary_seen == false) && ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ONLINE") == 1) online_seen= true;
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OFFLINE") == 1) online_seen= true;
    }
    if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENT")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_EVENT, TOKEN_REFTYPE_EVENT) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FUNCTION")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION, TOKEN_REFTYPE_FUNCTION) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX")))
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_INDEX,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");
        if (hparse_errno > 0) return;
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_algorithm_or_lock();
      }
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PREPARE")))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_STATEMENT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCEDURE")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROLE")== 1))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      do
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ROLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEMA")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SERVER")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_SERVER, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else if ((online_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE")))
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      do
      {
        if (hparse_errno > 0) return;
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      if (hparse_errno > 0) return;
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RESTRICT");
      if (hparse_errno > 0) return;
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CASCADE");
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLESPACE")))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_TABLESPACE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINE"))
      {
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      }
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRIGGER")))
    {
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TRIGGER, TOKEN_REFTYPE_TRIGGER) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if ((temporary_seen == false) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER")))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      do
      {
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else if (((temporary_seen == false || (hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)) && (online_seen == false) && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW")))
    {
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF_IN_IF_EXISTS, "IF") == 1)
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXISTS");
        if (hparse_errno > 0) return;
      }
      do
      {
        if (hparse_errno > 0) return;
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_VIEW, TOKEN_REFTYPE_VIEW) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      if (hparse_errno > 0) return;
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RESTRICT");
      if (hparse_errno > 0) return;
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CASCADE");
      if (hparse_errno > 0) return;
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END"))
  {
    hparse_statement_type= TOKEN_KEYWORD_COMMIT;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_commit_or_rollback();
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EXECUTE, "EXECUTE") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_EXECUTE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MARIADB_10_2_3, TOKEN_REFTYPE_ANY, TOKEN_KEYWORD_IMMEDIATE, "IMMEDIATE") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MARIADB_10_2_3, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 0)
      hparse_f_expect(FLAG_VERSION_MARIADB_10_2_3, TOKEN_REFTYPE_USER_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    }
    else hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_STATEMENT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USING"))
    {
      do
      {
       hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_USER_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
       if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EXPLAIN, "EXPLAIN"))
  {
    hparse_statement_type= TOKEN_KEYWORD_EXPLAIN;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EXPLAIN, "QUERY") == 1)
    {
      hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EXPLAIN, "PLAN");
      if (hparse_errno > 0) return;
    }
    hparse_f_explain_or_describe(block_top);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FLUSH, "FLUSH") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_FLUSH;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO_WRITE_TO_BINLOG") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL") == 1)) {;}
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES") == 1)
    {
      bool table_name_seen= false, comma_seen= false;
      for (;;)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0)
        {
          if (hparse_errno > 0) return;
          if (comma_seen == true) hparse_f_error();
          if (hparse_errno > 0) return;
          break;
        }
        table_name_seen= true;
        comma_seen= false;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, ","))
        {
          comma_seen= true;
          continue;
        }
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCK");
        if (hparse_errno > 0) return;
      }
      else if ((table_name_seen == true) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1))
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXPORT");
        if (hparse_errno > 0) return;
      }
    }
    else do
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DES_KEY_FILE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINE") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ERROR") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GENERAL") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOSTS") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPTIMIZER_COSTS") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRIVILEGES") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CACHE");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELAY") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
        if (hparse_errno > 0) return;
        hparse_f_for_channel();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLOW") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER_RESOURCES") == 1) {;}
      else hparse_f_error();
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_GET, "GET"))
  {
    hparse_statement_type= TOKEN_KEYWORD_GET;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURRENT");
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DIAGNOSTICS");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONDITION") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      do
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
        if (hparse_errno > 0) return;
        hparse_f_condition_information_item_name();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else do
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NUMBER") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROW_COUNT");
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_GRANT, "GRANT"))
  {
    hparse_statement_type= TOKEN_KEYWORD_GRANT;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    bool proxy_seen= false;
    bool role_name_seen= false;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROXY") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");
      if (hparse_errno > 0) return;
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      if (hparse_f_user_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      proxy_seen= true;
    }
    else
    {
      hparse_f_grant_or_revoke(TOKEN_KEYWORD_GRANT, &role_name_seen);
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_user_specification_list();
    if (hparse_errno > 0) return;
    hparse_f_require(TOKEN_KEYWORD_GRANT, proxy_seen, role_name_seen);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_HANDLER, "HANDLER"))
  {
    hparse_statement_type= TOKEN_KEYWORD_HANDLER;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPEN") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_HANDLER_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ") == 1)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIRST") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NEXT") == 1)) {;}
      else if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_INDEX) == 1)
      {
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<=") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">=") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<") == 1))
        {
          hparse_f_expression_list(TOKEN_KEYWORD_HANDLER);
          if (hparse_errno > 0) return;
        }
        else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIRST") == 1)
              || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NEXT") == 1)
              || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PREV") == 1)
              || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LAST") == 1))
           {;}
        else hparse_f_error();
        if (hparse_errno > 0) return;
      }
      hparse_f_where();
      hparse_f_limit(TOKEN_KEYWORD_HANDLER);
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CLOSE") == 1) {;}
    else hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_HELP, "HELP"))
  {
    hparse_statement_type= TOKEN_KEYWORD_HELP;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_INSERT, "INSERT"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_INSERT;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_subquery_is_allowed= true;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOW_PRIORITY") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELAYED") == 1) {;}
    else hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HIGH_PRIORITY");
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE");
    hparse_f_insert_or_replace();
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DUPLICATE");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UPDATE");
      if (hparse_errno > 0) return;
      hparse_f_assignment(TOKEN_KEYWORD_INSERT);
      if (hparse_errno > 0) return;
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_INSTALL, "INSTALL") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_INSTALL;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PLUGIN");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PLUGIN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PLUGIN") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PLUGIN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SONAME");
    if (hparse_errno > 0) return;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_KILL, "KILL"))
  {
    hparse_statement_type= TOKEN_KEYWORD_KILL;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HARD") == 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SOFT");
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONNECTION") == 0)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY") == 1)
      {
        if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ID");
      }
    }
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOAD, "LOAD"))
  {
    hparse_statement_type= TOKEN_KEYWORD_LOAD;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATA") == 1)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOW_PRIORITY") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONCURRENT") == 1))
        {;}
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL") == 1)
        {;}
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INFILE");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLACE") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1))
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
      if (hparse_errno > 0) return;
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      /* MariaDB manual doesn't mention partition clause but it's supported */
      hparse_f_partition_list(true, false);
      if (hparse_errno > 0) return;
      hparse_f_infile_or_outfile();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LINES") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROWS");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1) /* [(col_name_or_user_var...)] */
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        do
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_COLUMN_OR_USER_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
          if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
        hparse_f_assignment(TOKEN_KEYWORD_LOAD);
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CACHE");
      if (hparse_errno > 0) return;
      do
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_partition_list(true, true);
        if (hparse_errno > 0) return;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEY") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
          if (hparse_errno > 0) return;
          do
          {
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_INDEX,TOKEN_TYPE_IDENTIFIER, "[identifier]");
            if (hparse_errno > 0) return;
          } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
          if (hparse_errno > 0) return;
        }
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LEAVES");
          if (hparse_errno > 0) return;
        }
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "XML") == 1)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOW_PRIORITY") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONCURRENT") == 1)) {;}
      hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INFILE");
      if (hparse_errno > 0) return;
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPLACE") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1))
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
      if (hparse_errno > 0) return;
      main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
      if (hparse_errno > 0) return;
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_character_set() == 1)
      {
        if (hparse_f_character_set_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROWS") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IDENTIFIED");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BY");
        if (hparse_errno > 0) return;
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IGNORE") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LINES") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROWS") == 1)) {;}
        else hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        do
        {
          if (hparse_f_qualified_name_of_operand(false) == 0)
          {
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER,  "[identifier]");
          }
          if (hparse_errno > 0) return;
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        hparse_f_assignment(TOKEN_KEYWORD_LOAD);
        if (hparse_errno > 0) return;
      }
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOCK, "LOCK"))
  {
    hparse_statement_type= TOKEN_KEYWORD_LOCK;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES"); /* TABLE is undocumented */
    if (hparse_errno > 0) return;
    do
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AS") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ALIAS,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1) {;}
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ") == 1)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL") == 1) {;}
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOW_PRIORITY") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WRITE");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WRITE") == 1)
      {
        if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONCURRENT");
      }
      else hparse_f_error();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LUA, "LUA"))
  {
    hparse_statement_type= TOKEN_KEYWORD_LUA;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OPTIMIZE, "OPTIMIZE") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_OPTIMIZE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    int table_or_view;
    if (hparse_f_analyze_or_optimize(TOKEN_KEYWORD_OPTIMIZE, &table_or_view) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PRAGMA, "PRAGMA") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_PRAGMA;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    bool is_parenthesis_seen= false;
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1) is_parenthesis_seen= true;
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_LITERAL, "[literal]") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NO") == 1) {;}
    else hparse_f_error();
    if (hparse_errno > 0) return;
    if (is_parenthesis_seen == true)
    {
      hparse_f_expect(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PREPARE, "PREPARE") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_PREPARE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_STATEMENT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM");
    if (hparse_errno > 0) return;
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PURGE, "PURGE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_PURGE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BEFORE");
      if (hparse_errno > 0) return;
      hparse_f_opr_1(0); /* actually, should be "datetime expression" */
      if (hparse_errno > 0) return;
    }
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RELEASE, "RELEASE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_RELEASE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SAVEPOINT");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SAVEPOINT");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_SAVEPOINT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REINDEX, "REINDEX") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_REINDEX;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RENAME, "RENAME") == 1)
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_RENAME;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER"))
    {
      do
      {
        if (hparse_errno > 0) return;
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
        if (hparse_errno > 0) return;
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
      if (hparse_errno > 0) return;
      do
      {
        if (hparse_errno > 0) return;
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO");
        if (hparse_errno > 0) return;
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPAIR, "REPAIR") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_REPAIR;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    int table_or_view;
    if (hparse_f_analyze_or_optimize(TOKEN_KEYWORD_REPAIR, &table_or_view) == 1)
    {
      if (table_or_view == TOKEN_KEYWORD_TABLE)
      {
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUICK");
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXTENDED");
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USE_FRM");
      }
      if (table_or_view == TOKEN_KEYWORD_VIEW)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MYSQL");
          if (hparse_errno > 0) return;
        }
      }
    }
    else hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPLACE, "REPLACE"))
  {
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_statement_type= TOKEN_KEYWORD_REPLACE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    //hparse_statement_type= TOKEN_KEYWORD_INSERT;
    hparse_subquery_is_allowed= true;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOW_PRIORITY") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DELAYED") == 1) {;}
    hparse_f_insert_or_replace();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RESET, "RESET"))
  {
    hparse_statement_type= TOKEN_KEYWORD_RESET;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    do
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER") == 1)
      {
        if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
        {
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO") == 1)
          {
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
            if (hparse_errno > 0) return;
          }
        }
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY_CACHE") == 1) {;}
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVE") == 1)
      {
        if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
        {
          hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL");
        }
      }
      else hparse_f_error();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RESIGNAL, "RESIGNAL"))
  {
    hparse_statement_type= TOKEN_KEYWORD_RESIGNAL;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    /* We accept RESIGNAL even if we're not in a condition handler; we're just a recognizer. */
    hparse_f_signal_or_resignal(TOKEN_KEYWORD_RESIGNAL, block_top);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REVOKE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_REVOKE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    bool role_name_seen= false;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROXY") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ON");
      if (hparse_errno > 0) return;
      if (hparse_f_user_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else
    {
      hparse_f_grant_or_revoke(TOKEN_KEYWORD_REVOKE, &role_name_seen);
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM");
    if (hparse_errno > 0) return;
    do
    {
      if (hparse_f_user_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_ROLLBACK, "ROLLBACK") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_ROLLBACK;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WORK");
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TO") == 1)
    {
      /* it's not documented, but the word SAVEPOINT is optional */
      hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SAVEPOINT");
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_SAVEPOINT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      return;
    }
    else hparse_f_commit_or_rollback();
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SAVEPOINT, "SAVEPOINT") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_SAVEPOINT;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_SAVEPOINT,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    return;
  }
  else if (hparse_f_select(false) == 1)
  {
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SET, "SET"))
  {
    hparse_statement_type= TOKEN_KEYWORD_SET;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    bool global_seen= false;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GLOBAL") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SESSION") == 1)
     || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCAL") == 1))
    {
      global_seen= true;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRANSACTION") == 1)
    {
      bool isolation_seen= false, read_seen= false;
      do
      {
        if ((isolation_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ISOLATION") == 1))
        {
          isolation_seen= true;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LEVEL");
          if (hparse_errno > 0) return;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "REPEATABLE") == 1)
          {
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ");
            if (hparse_errno > 0) return;
          }
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ") == 1)
          {
            if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMITTED") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNCOMMITTED");
            if (hparse_errno > 0) return;
          }
          else hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SERIALIZABLE");
          if (hparse_errno > 0) return;
        }
        else if ((read_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ") == 1))
        {
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WRITE") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ONLY");
          if (hparse_errno > 0) return;
        }
        else hparse_f_error();
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      return;
    }
    if ((global_seen == false) && (hparse_f_character_set() == 1))
    {
      if (hparse_f_character_set_name() == 0)
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE"))
      {
        if (hparse_f_collation_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      return;
    }
    if (hparse_errno > 0) return;
    if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (global_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROLE");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NONE") == 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ROLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
      {
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      return;
    }
    if ((global_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NAMES") == 1))
    {
      if (hparse_f_character_set_name() == 0)
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATE"))
      {
        if (hparse_f_collation_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      return;
    }
    if ((global_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
      {
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
        if (hparse_errno > 0) return;
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
        if (hparse_errno > 0) return;
      }
      else
      {
        if (hparse_f_literal() == 0) hparse_f_error();
      }
      if (hparse_errno > 0) return;
      return;
    }
    if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (global_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROLE") == 1))
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NONE") == 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ROLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      }
      if (hparse_errno > 0) return;
      return;
    }
    if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (global_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATEMENT") == 1))
    {
      hparse_f_assignment(TOKEN_KEYWORD_SET);
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
      if (hparse_errno > 0) return;
      hparse_f_statement(block_top);
      if (hparse_errno > 0) return;
      return;
    }
    /* TODO: This fails to take "set autocommit = {0 | 1}" into account as special. */
    /* TODO: This fails to take "set sql_log_bin = {0 | 1}" into account as special. */
    hparse_f_assignment(TOKEN_KEYWORD_SET);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SHOW, "SHOW") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_SHOW;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVES");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "AUTHORS") == 1) {;} /* removed in MySQL 5.6.8 */
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINARY") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BINLOG") == 1) /* show binlog */
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENTS");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1)
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      hparse_f_limit(TOKEN_KEYWORD_SHOW);
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_character_set() == 1) /* show character set */
    {
      hparse_f_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_errno > 0) return;
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CLIENT_STATISTICS") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLLATION") == 1) /* show collation */
    {
      hparse_f_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMNS") == 1) /* show columns */
    {
      hparse_f_show_columns();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONTRIBUTORS") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COUNT") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "*");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ERRORS") == 1) ;
      else
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WARNINGS");
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CREATE") == 1) /* show create ... */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASE") == 1)
      {
        hparse_f_if_not_exists();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENT") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_EVENT, TOKEN_REFTYPE_EVENT) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXPLAIN") == 1))
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FUNCTION") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION, TOKEN_REFTYPE_FUNCTION) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCEDURE") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEMA") == 1)
      {
        hparse_f_if_not_exists();
        if (hparse_errno > 0) return;
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRIGGER") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TRIGGER, TOKEN_REFTYPE_TRIGGER) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (((hparse_dbms_mask & (FLAG_VERSION_MYSQL_5_7|FLAG_VERSION_MARIADB_10_2_2)) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER") == 1))
      {
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VIEW") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_VIEW, TOKEN_REFTYPE_VIEW) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DATABASES") == 1) /* show databases */
    {
      hparse_f_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINE") == 1) /* show engine */
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ENGINE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MUTEX");
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINES") == 1) /* show engines */
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ERRORS") == 1) /* show errors */
    {
      hparse_f_limit(TOKEN_KEYWORD_SHOW);
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENTS") == 1) /* show events */
    {
      hparse_f_from_or_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXPLAIN") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIELDS") == 1) /* show columns */
    {
      hparse_f_show_columns();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FULL") == 1) /* show full [columns|tables|etc.] */
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COLUMNS") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FIELDS") == 1))
      {
        hparse_f_show_columns();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES") == 1)
      {
        hparse_f_from_or_like_or_where();
        if (hparse_errno > 0) return;
      }
      else hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCESSLIST");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FUNCTION") == 1) /* show function [code] */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CODE") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION, TOKEN_REFTYPE_FUNCTION) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 1)
      {
        hparse_f_like_or_where();
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GLOBAL") == 1) /* show global ... */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 1)
      {
        hparse_f_like_or_where();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARIABLES") == 1)
      {
        hparse_f_like_or_where();
        if (hparse_errno > 0) return;
      }
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GRANTS") == 1) /* show grants */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
      {
        if (hparse_f_user_name() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX") == 1) /* show index */
    {
      hparse_f_indexes_or_keys();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEXES") == 1) /* show indexes */
    {
      hparse_f_indexes_or_keys();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INDEX_STATISTICS") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "KEYS") == 1) /* show keys */
    {
      hparse_f_indexes_or_keys();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOCALES") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER") == 1) /* show master [status|logs\ */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 0)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LOGS");
      }
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OPEN") == 1) /* show open [tables] */
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES");
      if (hparse_errno > 0) return;
      hparse_f_from_or_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PLUGINS") == 1) /* show plugins */
    {
      if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SONAME") == 1))
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1) {;}
        else hparse_f_from_or_like_or_where();
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PRIVILEGES") == 1) /* show privileges */
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCEDURE") == 1) /* show procedure [code|status] */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CODE") == 1)
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_PROCEDURE, TOKEN_REFTYPE_PROCEDURE) == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 1)
      {
        hparse_f_like_or_where();
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROCESSLIST") == 1) /* show processlist */
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROFILE") == 1) /* show profile */
    {
      for (;;)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BLOCK") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IO");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BLOCK_IO") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONTEXT") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SWITCHES");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CPU") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IPC") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MEMORY") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PAGE") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FAULTS");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SOURCE") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SWAPS") == 1) {;}
        else break;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1) continue;
        break;
      }
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY");
        if (hparse_errno > 0) return;
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      hparse_f_limit(TOKEN_KEYWORD_SHOW);
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PROFILES") == 1) /* show profiles */
    {
      ;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "QUERY_RESPONSE_TIME") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELAYLOG") == 1) /* show relaylog */
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EVENTS");
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IN") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM") == 1)
      {
        if (hparse_f_literal() == 0) hparse_f_error();
        if (hparse_errno > 0) return;
      }
      hparse_f_limit(TOKEN_KEYWORD_SHOW);
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SCHEMAS") == 1) /* show schemas */
    {
      hparse_f_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SESSION") == 1) /* show session ... */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 1)
      {
        hparse_f_like_or_where();
        if (hparse_errno > 0) return;
      }
      else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARIABLES") == 1)
      {
        hparse_f_like_or_where();
        if (hparse_errno > 0) return;
      }
      else hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVE") == 1) /* show slave */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HOSTS") == 1) {;}
      else
      {
        if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS");
        if (hparse_errno > 0) return;
        if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
        {
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NONBLOCKING") == 1) {;}
          hparse_f_for_channel();
          if (hparse_errno > 0) return;
        }
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS") == 1) /* show status */
    {
      hparse_f_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STORAGE") == 1) /* show storage */
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ENGINES");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 1) /* show table */
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS");
      if (hparse_errno > 0) return;
      hparse_f_from_or_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES") == 1) /* show tables */
    {
      hparse_f_from_or_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE_STATISTICS") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRIGGERS") == 1) /* show triggers */
    {
      hparse_f_from_or_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER_STATISTICS") == 1))
    {
      ;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARIABLES") == 1) /* show variables */
    {
      hparse_f_like_or_where();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WARNINGS") == 1) /* show warnings */
    {
      hparse_f_limit(TOKEN_KEYWORD_SHOW);
      if (hparse_errno > 0) return;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WSREP_MEMBERSHIP") == 1))
    {
      ;
    }
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WSREP_STATUS") == 1))
    {
      ;
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SHUTDOWN, "SHUTDOWN"))
  {
    hparse_statement_type= TOKEN_KEYWORD_SHUTDOWN;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SIGNAL, "SIGNAL"))
  {
    hparse_statement_type= TOKEN_KEYWORD_SIGNAL;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_signal_or_resignal(TOKEN_KEYWORD_SIGNAL, block_top) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SONAME, "SONAME") == 1))
  {
    hparse_statement_type= TOKEN_KEYWORD_SONAME;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_literal() == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_START, "START"))
  {
    hparse_statement_type= TOKEN_KEYWORD_START;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TRANSACTION") == 1)
    {
      if (hparse_errno > 0) return;
      bool with_seen= false, read_seen= false;
      do
      {
        if ((with_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WITH")))
        {
          with_seen= true;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONSISTENT");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SNAPSHOT");
          if (hparse_errno > 0) return;
        }
        if ((read_seen == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "READ")))
        {
          read_seen= true;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ONLY") == 1) ;
          else hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WRITE");
          if (hparse_errno > 0) return;
        }
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GROUP_REPLICATION") == 1) {;}
    else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1))
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVES");
      if (hparse_errno > 0) return;
      do
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IO_THREAD") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_THREAD") == 1) {;}
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVE") == 1)
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      do
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IO_THREAD") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_THREAD") == 1) {;}
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNTIL") == 1)
      {
        if (((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
         && ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_BEFORE_GTIDS") == 1)
          || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_AFTER_GTIDS") == 1)
          || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_AFTER_MTS_GAPS") == 1)))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_LOG_FILE") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_LOG_POS");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
        }
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELAY_LOG_FILE") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RELAY_LOG_POS");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
        }
        else if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MASTER_GTID_POS") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
          if (hparse_errno > 0) return;
        }
        else hparse_f_error();
        if (hparse_errno > 0) return;
      }
      if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
      {
        for (;;)
        {
          bool expect_something= false;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER") == 1) expect_something= true;
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PASSWORD") == 1) expect_something= true;
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT_AUTH") == 1) expect_something= true;
          else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PLUGIN_DIR") == 1) expect_something= true;
          else break;
          if (hparse_errno > 0) return;
          if (expect_something)
          {
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
            if (hparse_errno > 0) return;
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
            if (hparse_errno > 0) return;
          }
        }
        hparse_f_for_channel();
        if (hparse_errno > 0) return;
      }
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_STOP, "STOP") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_STOP;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "GROUP_REPLICATION") == 1) {;}
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ALL") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVES");
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SLAVE") == 1)
    {
      if (((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 1)) {;}
      do
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "IO_THREAD") == 1) {;}
        else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQL_THREAD") == 1) {;}
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
      if ((hparse_dbms_mask & FLAG_VERSION_MYSQL_ALL) != 0)
      {
        hparse_f_for_channel();
        if (hparse_errno > 0) return;
      }
    }
    else hparse_f_error();
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_TRUNCATE, "TRUNCATE"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_TRUNCATE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE");
    if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_TABLE, TOKEN_REFTYPE_TABLE) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_UNINSTALL, "UNINSTALL") == 1)
  {
    hparse_statement_type= TOKEN_KEYWORD_UNINSTALL;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PLUGIN") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_PLUGIN,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return;
    }
    else
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SONAME");
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_UNLOCK, "UNLOCK"))
  {
    hparse_statement_type= TOKEN_KEYWORD_UNLOCK;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLE") == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "TABLES"); /* TABLE is undocumented */
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_UPDATE, "UPDATE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_UPDATE;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "OR") == 1)
    {
      hparse_f_conflict_algorithm();
      if (hparse_errno > 0) return;
    }
    hparse_subquery_is_allowed= true;
    if (hparse_f_table_reference(0) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
    bool multi_seen= false;
    while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1)
    {
      multi_seen= true;
      if (hparse_f_table_reference(0) == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SET");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    hparse_f_assignment(TOKEN_KEYWORD_UPDATE);
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHERE") == 1)
    {
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return;
    }
    if (multi_seen == false)
    {
      if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
      {
        hparse_f_order_by(0);
        if (hparse_errno > 0) return;
        hparse_f_limit(TOKEN_KEYWORD_UPDATE);
        if (hparse_errno > 0) return;
      }
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_VACUUM, "VACUUM"))
  {
    hparse_statement_type= TOKEN_KEYWORD_VACUUM;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_TARANTOOL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_WITH, "WITH"))
  {
    hparse_statement_type= TOKEN_KEYWORD_WITH;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_with_clause(block_top);
    return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_XA, "XA"))
  {
    hparse_statement_type= TOKEN_KEYWORD_XA;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "START") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_BEGIN_XA, "BEGIN") == 1))
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "JOIN") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RESUME") == 1)) {;}
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "END") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SUSPEND") == 1)
      {
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR") == 1)
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "MIGRATE");
          if (hparse_errno > 0) return;
        }
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PREPARE") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "COMMIT") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ONE") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "PHASE");
        if (hparse_errno > 0) return;
      }
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ROLLBACK") == 1)
    {
      if (hparse_f_literal() == 0) hparse_f_error();
      if (hparse_errno > 0) return;
    }
    else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "RECOVER") == 1)
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONVERT") == 1)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "XID");
        if (hparse_errno > 0) return;
      }
    }
    else hparse_f_error();
  }
  else
  {
    if ((dbms_version_mask & FLAG_VERSION_TARANTOOL) != 0)
    {
      if (hparse_errno > 0) return;
      hparse_f_lua_blocklist(0, hparse_i);
    }
    else hparse_f_error();
  }
}


/*
  compound statement, or statement
  Pass: calling_statement_type = 0 (top level) | TOKEN_KEYWORD_FUNCTION/PROCEDURE/EVENT/TRIGGER
*/
void MainWindow::hparse_f_block(int calling_statement_type, int block_top)
{
  if (hparse_errno > 0) return;
  hparse_subquery_is_allowed= false;
  /*
    TODO:
      For labels + conditions + local variables, you could:
      push on stack when they come into scope
      pop from stack when they go out of scope
      check they're valid when you see reference
      show what they are when you see hover (requires showing where they're declared too)
      ... but currently we're saying any identifier will be okay
    But we're working on it. As a first step, for keeping track of
    scope, we have hparse_i_of_block = offset of last
    BEGIN|LOOP|WHILE|REPEAT|IF (or label that precedes that, if any).
    So when we reach END we can set main_token_pointers[] to point
    "back" to where a block started.
    Todo: Consider using the same technique for ()s and statements.
    Todo: Consider pointing forward as well as backward. */

  int hparse_i_of_block= -1;
  QString label= "";
  /* Label check. */
  /* Todo: most checks are illegal if preceded by a label. Check for that. */
  if (hparse_count_of_accepts != 0)
  {
    hparse_f_next_nexttoken();
    if (hparse_next_token == ":")
    {
      label= hparse_token;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_DEFINE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
      hparse_i_of_block= hparse_i_of_last_accepted;
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ":");
      if (hparse_errno > 0) return;
    }
  }
  /*
    BEGIN could be the start of a BEGIN END block, but
    "BEGIN;" or "BEGIN WORK" are start-transaction statements.
    Ugly.
    Todo: See what happens if next is \G or delimiter.
  */
  bool next_is_semicolon_or_work= false;
  hparse_f_next_nexttoken();
  if ((hparse_next_token == ";")
   || (hparse_next_token == hparse_delimiter_str)
   || (QString::compare(hparse_next_token, "WORK", Qt::CaseInsensitive) == 0))
  {
    next_is_semicolon_or_work= true;
  }

  //int hparse_i_of_start= hparse_i;
  if ((next_is_semicolon_or_work == false) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_BEGIN, "BEGIN") == 1))
  {
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    hparse_statement_type= TOKEN_KEYWORD_BEGIN;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_begin_seen= true;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ATOMIC");
      if (hparse_errno > 0) return;
    }
    else
    {
      /* The MariaDB parser cannot handle top-level BEGIN without NOT, so we don't either. */
      if (hparse_count_of_accepts < 2)
      {
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WORK"); /* impossible but enhances expected_list */
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");   /* impossible but enhances expected_list */
        hparse_f_error();
        return;
      }
    }
    for (;;)                                                            /* DECLARE statements */
    {
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DECLARE") == 1)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
        if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONTINUE") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "EXIT") == 1)
         || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNDO") == 1))
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "HANDLER");
          if (hparse_errno > 0) return;
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
          if (hparse_errno > 0) return;
          do
          {
            if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQLSTATE") == 1)
            {
              hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUE");
              if (hparse_f_literal() == 0) hparse_f_error();
              if (hparse_errno > 0) return;
            }
            else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQLWARNING") == 1) {;}
            else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NOT") == 1)
            {
              hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOUND");
              if (hparse_errno > 0) return;
            }
            else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQLEXCEPTION") == 1) {;}
            else if (hparse_f_conditions(block_top) == 1) {;}
            else
            {
              if (hparse_f_literal() == 0) hparse_f_error();
            }
            if (hparse_errno > 0) return;
          } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
          hparse_f_block(calling_statement_type, block_top);
          continue;
        }
        int identifier_count= 0;
        bool condition_seen= false;
        bool cursor_seen= false;
        do
        {
          hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CONDITION_OR_CURSOR,TOKEN_TYPE_IDENTIFIER, "[identifier]");
          int hparse_i_of_identifier= hparse_i_of_last_accepted;
          if (hparse_errno > 0) return;
          ++identifier_count;
          if ((identifier_count == 1) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CONDITION") == 1))
          {
            main_token_reftypes[hparse_i_of_identifier]= TOKEN_REFTYPE_CONDITION_DEFINE;
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
            if (hparse_errno > 0) return;
            if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SQLSTATE") == 1)
            {
              hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VALUE");
            }
            if (hparse_f_literal() == 0) hparse_f_error();
            if (hparse_errno > 0) return;
            condition_seen= true;
            break;
          }
          if ((identifier_count == 1) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CURSOR") == 1))
          {
            main_token_reftypes[hparse_i_of_identifier]= TOKEN_REFTYPE_CURSOR_DEFINE;
            hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FOR");
            if (hparse_errno > 0) return;
            if (hparse_f_select(false) == 0)
            {
              hparse_f_error();
              return;
            }
            cursor_seen= true;
          }
          else
          {
            main_token_reftypes[hparse_i_of_identifier]= TOKEN_REFTYPE_VARIABLE_DEFINE;
          }
        } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        if (condition_seen == true) {;}
        else if (cursor_seen == true) {;}
        else
        {
          if (hparse_f_data_type() == -1) hparse_f_error();
          if (hparse_errno > 0) return;
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DEFAULT") == 1)
          {
            if (hparse_f_literal() == 0) hparse_f_error(); /* todo: must it really be a literal? */
            if (hparse_errno > 0) return;
          }
        }
        hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");
        if (hparse_errno > 0) return;
      }
      else break;
    }
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 0)
    {
      for (;;)
      {
        hparse_f_block(calling_statement_type, block_top);
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1) break;
      }
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER,TOKEN_TYPE_IDENTIFIER, label);
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CASE, "CASE") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    int when_count= 0;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN") == 0)
     {
      hparse_f_opr_1(0); /* not compulsory */
      if (hparse_errno > 0) return;
    }
    else when_count= 1;
    if (when_count == 0)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN");
      if (hparse_errno > 0) return;
    }
    for (;;)
    {
      hparse_subquery_is_allowed= true;
      hparse_f_opr_1(0);
      hparse_subquery_is_allowed= false;
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "THEN");
      if (hparse_errno > 0) return;
      int break_word= 0;
      for (;;)
      {
        hparse_f_block(calling_statement_type, block_top);
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1)
        {
          break_word= TOKEN_KEYWORD_END; break;
        }
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "WHEN") == 1)
        {
          break_word= TOKEN_KEYWORD_WHEN; break;
        }
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ELSE") == 1)
        {
          break_word= TOKEN_KEYWORD_ELSE; break;
        }
      }
      if (break_word == TOKEN_KEYWORD_END) break;
      if (break_word == TOKEN_KEYWORD_WHEN) continue;
      assert(break_word == TOKEN_KEYWORD_ELSE);
      for (;;)
      {
        hparse_f_block(calling_statement_type, block_top);
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1) break;
      }
      break;
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CASE, "CASE");
    if (hparse_errno > 0) return;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER,TOKEN_TYPE_IDENTIFIER, label);
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF, "IF") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    for (;;)
    {
      hparse_subquery_is_allowed= true;
      hparse_f_opr_1(0);
      hparse_subquery_is_allowed= false;
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "THEN");
      if (hparse_errno > 0) return;
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
      int break_word= 0;
      for (;;)
      {
        hparse_f_block(calling_statement_type, block_top);
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1)
        {
          break_word= TOKEN_KEYWORD_END; break;
        }
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ELSEIF") == 1)
        {
          break_word= TOKEN_KEYWORD_ELSEIF; break;
        }
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "ELSE") == 1)
        {
          break_word= TOKEN_KEYWORD_ELSE; break;
        }
      }
      if (break_word == TOKEN_KEYWORD_END)
      {
        break;
      }
      if (break_word == TOKEN_KEYWORD_ELSEIF)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
        continue;
      }
      assert(break_word == TOKEN_KEYWORD_ELSE);
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
      for (;;)
      {
        hparse_f_block(calling_statement_type, block_top);
        if (hparse_errno > 0) return;
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1) break;
      }
      break;
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF, "IF");
    if (hparse_errno > 0) return;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER,TOKEN_TYPE_IDENTIFIER, label);
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOOP, "LOOP") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    for (;;)
    {
      hparse_f_block(calling_statement_type, block_top);
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1) break;
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOOP, "LOOP");
    if (hparse_errno > 0) return;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER,TOKEN_TYPE_IDENTIFIER, label);
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPEAT, "REPEAT") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    for (;;)
    {
      hparse_f_block(calling_statement_type, block_top);
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "UNTIL") == 1) break;
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    hparse_f_opr_1(0);
    hparse_subquery_is_allowed= false;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPEAT, "REPEAT");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER,TOKEN_TYPE_IDENTIFIER, label)) return;
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_ITERATE, "ITERATE") == 1) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "LEAVE") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_labels(block_top);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");
    if (hparse_errno > 0) return;
  }
  else if ((hparse_begin_seen == true) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CLOSE, "CLOSE") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_cursors(block_top);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");
    if (hparse_errno > 0) return;
  }
  else if ((hparse_begin_seen == true) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FETCH, "FETCH") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "NEXT") == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM");
      if (hparse_errno > 0) return;
    }
    else hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "FROM");
    hparse_f_cursors(block_top);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "INTO");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_CLAUSE;
    do
    {
      hparse_f_variables(true);
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");
    if (hparse_errno > 0) return;
  }
  else if ((hparse_begin_seen == true) && (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_OPEN, "OPEN") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_cursors(block_top);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");
    if (hparse_errno > 0) return;
  }
  else if ((calling_statement_type == TOKEN_KEYWORD_FUNCTION)
        && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RETURN, "RETURN") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    hparse_f_opr_1(0);
    if (hparse_errno > 0) return;
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_WHILE, "WHILE") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    hparse_subquery_is_allowed= true;
    hparse_f_opr_1(0);
    hparse_subquery_is_allowed= false;
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "DO");
    if (hparse_errno > 0) return;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    for (;;)
    {
      hparse_f_block(calling_statement_type, block_top);
      if (hparse_errno > 0) return;
      if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "END") == 1) break;
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_WHILE, "WHILE");
    if (hparse_errno > 0) return;
    hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER,TOKEN_TYPE_IDENTIFIER, label);
    if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    if (hparse_errno > 0) return;
  }
  else
  {
    hparse_f_statement(block_top);
    if (hparse_errno > 0) return;
    /* This kludge occurs more than once. */
    if ((hparse_prev_token != ";") && (hparse_prev_token != hparse_delimiter_str))
    {
      if (hparse_f_semicolon_and_or_delimiter(calling_statement_type) == 0) hparse_f_error();
    }
    if (hparse_errno > 0) return;
    return;
  }
}

#ifdef DBMS_TARANTOOL
/*
  From the Lua bnf https://www.lua.org/manual/5.1/manual.html

  todo: we still consider # to be a comment rather than an operator

*/
/*
  stat ::=  varlist `=´ explist |
  functioncall |
  do block end |
  while exp do block end |
  repeat block until exp |
  if exp then block {elseif exp then block} [else block] end |
  for Name `=´ exp `,´ exp [`,´ exp] do block end |
  for namelist in explist do block end |
  function funcname funcbody |
  local function Name funcbody |
  local namelist [`=´ explist]
*/
static int lua_calling_statement_type, lua_block_top;
static int lua_depth;

void MainWindow::hparse_f_lua_blocklist(int calling_statement_type, int block_top)
{
  int saved_hparse_i= hparse_i;
  unsigned short int saved_hparse_dbms_mask= hparse_dbms_mask;
  lua_depth= 0;
  hparse_dbms_mask= FLAG_VERSION_LUA;
  hparse_f_lua_blockseries(calling_statement_type, block_top, false);
  hparse_dbms_mask= saved_hparse_dbms_mask;
  if (hparse_errno > 0) return;
  main_token_flags[saved_hparse_i]|= TOKEN_FLAG_IS_LUA;
}
/* 0 or more statements or blocks of statements, optional semicolons */
void MainWindow::hparse_f_lua_blockseries(int calling_statement_type, int block_top, bool is_in_loop)
{
  int statement_type;
  ++lua_depth;
  for (;;)
  {
    statement_type= hparse_f_lua_block(calling_statement_type, block_top, is_in_loop);
    if (statement_type == 0) break;
    assert(lua_depth >= 0);
    /* todo: if "break" or "return", can anything follow? */
    /* This kludge occurs more than once. */
    if ((hparse_prev_token != ";") && (hparse_prev_token != hparse_delimiter_str))
    {
      hparse_f_semicolon_and_or_delimiter(calling_statement_type);
      if (hparse_errno > 0) return;
    }
    if (hparse_prev_token == hparse_delimiter_str)
    {
      if (hparse_delimiter_str != ";") return;
      if (lua_depth == 1) return;
    }
  }
  --lua_depth;
}
int MainWindow::hparse_f_lua_block(int calling_statement_type, int block_top, bool is_in_loop)
{
  lua_calling_statement_type= calling_statement_type;
  lua_block_top= block_top;
  if (hparse_errno > 0) return 0;
  hparse_subquery_is_allowed= false;
  int hparse_i_of_block= -1;
  if ((is_in_loop == true)
   && (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_BREAK, "break") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    return TOKEN_KEYWORD_BREAK;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DO_LUA, "do") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    hparse_f_lua_blockseries(calling_statement_type, block_top, is_in_loop);
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "end");
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_DO;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FOR, "for") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno != 0) return 0;
    if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ",") == 1)
    {
      if (hparse_f_lua_namelist() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IN, "in");
      if (hparse_errno != 0) return 0;
    }
    else
    {
      hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_errno != 0) return 0;
    }
    if (hparse_f_lua_explist() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_FUNCTION,TOKEN_KEYWORD_DO, "do");
    if (hparse_errno > 0) return 0;
    hparse_f_lua_blockseries(calling_statement_type, block_top, true);
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "end");
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_FOR;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FUNCTION, "function") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    if (hparse_f_lua_funcname() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    if (hparse_f_lua_funcbody() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_FUNCTION;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_IF, "if") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    main_token_flags[hparse_i_of_last_accepted] &= (~TOKEN_FLAG_IS_FUNCTION);
    for (;;)
    {
      hparse_subquery_is_allowed= true;
      hparse_f_lua_exp();
      if (hparse_errno > 0) return 0;
      hparse_subquery_is_allowed= false;
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "then");
      if (hparse_errno > 0) return 0;
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
      int break_word= 0;
      hparse_f_lua_blockseries(calling_statement_type, block_top, is_in_loop);
      if (hparse_errno > 0) return 0;
      if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "end") == 1)
      {
        break_word= TOKEN_KEYWORD_END;
      }
      else if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "elseif") == 1)
      {
        break_word= TOKEN_KEYWORD_ELSEIF;
      }
      else if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "else") == 1)
      {
        break_word= TOKEN_KEYWORD_ELSE;
      }
      else
      {
        hparse_f_error();
        if (hparse_errno > 0) return 0;
      }
      if (break_word == TOKEN_KEYWORD_END)
      {
        break;
      }
      if (break_word == TOKEN_KEYWORD_ELSEIF)
      {
        main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
        continue;
      }
      assert(break_word == TOKEN_KEYWORD_ELSE);
      main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
      hparse_f_lua_blockseries(calling_statement_type, block_top, is_in_loop);
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "end");
      if (hparse_errno > 0) return 0;
      break;
    }
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_IF;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_LOCAL, "local") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_FUNCTION, "function") == 1)
    {
      if (hparse_f_lua_name() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      if (hparse_f_lua_funcbody() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    else
    {
      if (hparse_f_lua_namelist() == 0) hparse_f_error();
      if (hparse_errno != 0) return 0;
      if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=") == 1)
      {
        if (hparse_errno > 0) return 0;
        if (hparse_f_lua_explist() == 0) hparse_f_error();
        if (hparse_errno > 0) return 0;
      }
    }
    return TOKEN_KEYWORD_LOCAL;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REPEAT, "repeat") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    hparse_f_lua_blockseries(calling_statement_type, block_top, true);
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "until");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    hparse_f_lua_exp();
    if (hparse_errno > 0) return 0;
    hparse_subquery_is_allowed= false;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "end");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_REPEAT;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_RETURN, "return") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_subquery_is_allowed= true;
    hparse_f_lua_explist();
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_RETURN;
  }
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_WHILE, "while") == 1)
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_i_of_block == -1) hparse_i_of_block= hparse_i_of_last_accepted;
    hparse_subquery_is_allowed= true;
    hparse_f_lua_exp();
    if (hparse_errno > 0) return 0;
    hparse_subquery_is_allowed= false;
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DO, "do");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_lua_blockseries(calling_statement_type, block_top, true);
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_END, "end");
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_pointers[hparse_i_of_last_accepted]= hparse_i_of_block;
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_WHILE;
  }
  int result_of_functioncall= hparse_f_lua_functioncall();
  if (hparse_errno > 0) return 0;
  if (result_of_functioncall == 1)
  {
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
    if (hparse_errno > 0) return 0;
    if (hparse_f_lua_explist() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    return TOKEN_KEYWORD_DECLARE;
  }
  if (result_of_functioncall == 2)
  {
    return TOKEN_KEYWORD_CALL;
  }
  /* todo: hparse_f_statement will fail because of hparse_dbms_mask */
  //hparse_f_statement(block_top);
  //if (hparse_errno > 0) return 0;
  return 0;
}
/* funcname ::= Name {`.´ Name} [`:´ Name] */
int MainWindow::hparse_f_lua_funcname()
{
  do
  {
    if (hparse_f_lua_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
  } while (hparse_f_lua_accept_dotted(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ".") == 1);
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ":") == 1)
  {
    if (hparse_f_lua_name() == 0) hparse_f_error();
  }
  return 1;
}
/* varlist ::= var {`,´ var} */
int MainWindow::hparse_f_lua_varlist()
{
  do
  {
    if (hparse_f_lua_var() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
  }
  while (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ",") == 1);
  return 1;
}
/* var ::=  Name | prefixexp `[´ exp `]´ | prefixexp `.´ Name */
int MainWindow::hparse_f_lua_var()
{
  if ((hparse_f_lua_name() == 1)
   || (hparse_f_lua_name_equivalent() == 1))
  {
    if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "[") == 1)
    {
      hparse_f_lua_exp();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "]");
      if (hparse_errno > 0) return 0;
    }
    if (hparse_f_lua_accept_dotted(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ".") == 1)
    {
      if (hparse_f_lua_var() == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      return 1;
    }
    return 1;
  }
  return 0;
}
/* namelist ::= Name {`,´ Name} */
int MainWindow::hparse_f_lua_namelist()
{
  do
  {
    if (hparse_f_lua_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
  } while (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
  return 1;
}
/* explist ::= {exp `,´} exp */
int MainWindow::hparse_f_lua_explist()
{
  do
  {
    if (hparse_f_lua_exp() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
  } while (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
   return 1;
}
/*
  exp ::=  nil | false | true | Number | String | `...´ | function |
           prefixexp | tableconstructor | exp binop exp | unop exp
*/
int MainWindow::hparse_f_lua_exp()
{
  //if (hparse_f_lua_prefixexp() == 1) return 1;
  //if (hparse_errno > 0) return 0;
  hparse_f_lua_opr_1(0);
  if (hparse_errno > 0) return 0;
  return 1;
}
/* prefixexp ::= var | functioncall | `(´ exp `)´ */
/* todo: this is never called. remove? */
int MainWindow::hparse_f_lua_prefixexp()
{
  if (hparse_f_lua_var() == 1) return 1;
  if (hparse_errno > 0) return 0;
  if (hparse_f_lua_functioncall() == 1) return 1;
  if (hparse_errno > 0) return 0;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(") == 0)
  {
    if (hparse_f_lua_exp() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return 0;
  }
  return 0;
}
/* functioncall ::=  prefixexp args | prefixexp `:´ Name args */
/*
  The return differs from other hparse_f_lua functions.
  Return: 0 neither, 1 var, not function, 2 function
*/
int MainWindow::hparse_f_lua_functioncall()
{
  bool is_var;
  if (hparse_f_lua_var() == 0) return 0;
  if (hparse_errno > 0) return 0;
so_far_it_is_a_var:
  if (hparse_f_lua_args() == 1) goto so_far_it_is_a_functioncall;
  if (hparse_errno > 0) return 0;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ":") == 1)
  {
    if (hparse_f_lua_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    if (hparse_f_lua_args() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    goto so_far_it_is_a_functioncall;
  }
  return 1;
so_far_it_is_a_functioncall:
  is_var= false;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "[") == 1)
  {
    hparse_f_lua_exp();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "]");
    if (hparse_errno > 0) return 0;
    is_var= true;
  }
  if (hparse_f_lua_accept_dotted(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ".") == 1)
  {
    if (hparse_f_lua_var() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    is_var= true;
  }
  if (is_var) goto so_far_it_is_a_var;
  return 2;
}
/* args ::=  `(´ [explist] `)´ | tableconstructor | String */
int MainWindow::hparse_f_lua_args()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")") == 0)
    {
      hparse_f_lua_explist();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")");
      if (hparse_errno > 0) return 0;
    }
    return 1;
  }
  if (hparse_f_lua_tableconstructor() == 1) return 1;
  if (hparse_errno > 0) return 0;
  if (hparse_f_lua_string() == 1) return 1;
  return 0;
}
/* function ::= function funcbody */
int MainWindow::hparse_f_lua_function()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_KEYWORD_FUNCTION, "function") == 0)
  {
    if (hparse_f_lua_funcbody() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}
/* funcbody ::= `(´ [parlist] `)´ block end */
int MainWindow::hparse_f_lua_funcbody()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(") == 0)
  {
    hparse_f_lua_parlist();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return 0;
    hparse_f_lua_blockseries(lua_calling_statement_type, lua_block_top, false);
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_KEYWORD_END, "end");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}
/* parlist ::= namelist [`,´ `...´] | `...´ */
int MainWindow::hparse_f_lua_parlist()
{
  return hparse_f_lua_namelist();
}
/* tableconstructor ::= `{´ [fieldlist] `}´ */
int MainWindow::hparse_f_lua_tableconstructor()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "{") == 0)
    return 0;
  if (hparse_f_lua_fieldlist() == 0) hparse_f_error();
  hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "}");
  if (hparse_errno > 0) return 0;
  return 1;
}
/* fieldlist ::= field {fieldsep field} [fieldsep] */
int MainWindow::hparse_f_lua_fieldlist()
{
  do
  {
    hparse_f_lua_field();
    if (hparse_errno > 0) return 0;
  }
  while (hparse_f_lua_fieldsep() == 1);
  if (hparse_errno > 0) return 0;
  return 1;
}
/* field ::= `[´ exp `]´ `=´ exp | Name `=´ exp | exp */
int MainWindow::hparse_f_lua_field()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "[") == 1)
  {
    if (hparse_f_lua_exp() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "]");
    if (hparse_errno > 0) return 0;
    return 1;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=");
    if (hparse_errno > 0) return 0;
    if (hparse_f_lua_exp() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    return 1;
  }
  hparse_f_next_nexttoken();
  if (hparse_next_token == "=")
  {
    if (hparse_f_lua_name() == 0) hparse_f_error();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=");
    if (hparse_errno > 0) return 0;
    hparse_f_lua_exp();
    if (hparse_errno > 0) return 0;
    return 1;
  }
  if (hparse_f_lua_exp() == 1) return 1;
  return 0;
}
/* fieldsep ::= `,´ | `;´ */
int MainWindow::hparse_f_lua_fieldsep()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ",") == 1) return 1;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ";") == 1) return 1;
  return 0;
}
/* Name = "any string of letters, digits, and underscores, not beginning with a digit. */
int MainWindow::hparse_f_lua_name()
{
  return hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_IDENTIFIER, "[identifier]");
}
/* (exp).name and (exp)[x] are variables, but (exp) is not a name */
int MainWindow::hparse_f_lua_name_equivalent()
{
  int i= hparse_i;
  QString token;
  token= hparse_text_copy.mid(main_token_offsets[i], main_token_lengths[i]);
  if (token != "(")
  {
    hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(");
    return 0;
  }
  int parentheses_count= 0;
  for (;;)
  {
    token= hparse_text_copy.mid(main_token_offsets[i], main_token_lengths[i]);
    if (token == "(") ++parentheses_count;
    if (token == ")")
    {
      --parentheses_count;
      if (parentheses_count == 0) break;
    }
    if (token == "") break;
    ++i;
  }
  token= hparse_text_copy.mid(main_token_offsets[i + 1], main_token_lengths[i + 1]);
  if ((token != "[") && (token != ".") && (token != "")) return 0;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(") == 1)
  {
    hparse_f_lua_exp();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return 0;
    return 1;
  }
  return 0;
}
/* Number ::= decimals and approximates ok. todo: 0xff */
int MainWindow::hparse_f_lua_number()
{
  if (main_token_types[hparse_i] == TOKEN_TYPE_LITERAL_WITH_DIGIT)
  {
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL_WITH_DIGIT, "[literal]"); /* guaranteed to succeed */
    if (hparse_errno > 0) return 0;
    return 1;
  }
  hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[number]"); /* guaranteed to fail */
  return 0;
}
/* String :: = in 's or "s or (todo:) [[...]]s or [==...]==]s */
int MainWindow::hparse_f_lua_string()
{
  if ((main_token_types[hparse_i] == TOKEN_TYPE_LITERAL_WITH_SINGLE_QUOTE)
   || (main_token_types[hparse_i] == TOKEN_TYPE_LITERAL_WITH_BRACKET)
   || (main_token_types[hparse_i] == TOKEN_TYPE_LITERAL_WITH_DOUBLE_QUOTE))
  {
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]"); /* guaranteed to succeed */
    if (hparse_errno > 0) return 0;
    return 1;
  }
  hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[string]"); /* guaranteed to fail */
  return 0;
}
int MainWindow::hparse_f_lua_literal()
{
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_KEYWORD_NIL, "nil") == 1) return 1;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_KEYWORD_FALSE, "false") == 1) return 1;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY, TOKEN_KEYWORD_NIL, "true") == 1) return 1;
  if (hparse_f_lua_number() == 1) return 1;
  if (hparse_errno > 0) return 0;
  if (hparse_f_lua_string() == 1) return 1;
  if (hparse_errno > 0) return 0;
  return 0;
}

void MainWindow::hparse_f_lua_opr_1(int who_is_calling) /* Precedence = 1 (bottom) */
{
  hparse_f_lua_opr_2(who_is_calling);
}

void MainWindow::hparse_f_lua_opr_2(int who_is_calling) /* Precedence = 2 */
{
  hparse_f_lua_opr_3(who_is_calling);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "or") == 1)
  {
    hparse_f_lua_opr_3(who_is_calling);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_lua_opr_3(int who_is_calling) /* Precedence = 3 */
{
  hparse_f_lua_opr_4(who_is_calling);
}

void MainWindow::hparse_f_lua_opr_4(int who_is_calling) /* Precedence = 4 */
{
  hparse_f_lua_opr_5(who_is_calling);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "and") == 1)
  {
    hparse_f_lua_opr_5(who_is_calling);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_lua_opr_5(int who_is_calling) /* Precedence = 5 */
{
  hparse_f_lua_opr_6(who_is_calling);
}

void MainWindow::hparse_f_lua_opr_6(int who_is_calling) /* Precedence = 6 */
{
  hparse_f_lua_opr_7(who_is_calling);
}

/* Most comp-ops can be chained e.g. "a <> b <> c", but not LIKE or IN. */
void MainWindow::hparse_f_lua_opr_7(int who_is_calling) /* Precedence = 7 */
{
  if (hparse_f_is_equal(hparse_token, "(")) hparse_f_lua_opr_8(who_is_calling, ALLOW_FLAG_IS_MULTI);
  else hparse_f_lua_opr_8(who_is_calling, 0);
  if (hparse_errno > 0) return;
  for (;;)
  {
    if ((hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<") == 1)
     || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">") == 1)
     || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "<=") == 1)
     || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ">=") == 1)
     || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "~=") == 1)
     || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "==") == 1))
    {
      if (hparse_f_is_equal(hparse_token, "(")) hparse_f_lua_opr_8(who_is_calling, ALLOW_FLAG_IS_MULTI);
      else hparse_f_lua_opr_8(who_is_calling, 0);
      if (hparse_errno > 0) return;
      continue;
    }
    break;
  }
}

void MainWindow::hparse_f_lua_opr_8(int who_is_calling, int allow_flags) /* Precedence = 8 */
{
  if (hparse_errno > 0) return;
  hparse_f_lua_opr_9(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while (hparse_f_lua_accept_dotted(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "..") == 1)
  {
    hparse_f_lua_opr_9(who_is_calling, allow_flags);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_lua_opr_9(int who_is_calling, int allow_flags) /* Precedence = 9 */
{
  hparse_f_lua_opr_10(who_is_calling, allow_flags);
}

void MainWindow::hparse_f_lua_opr_10(int who_is_calling, int allow_flags) /* Precedence = 10 */
{
  hparse_f_lua_opr_11(who_is_calling, allow_flags);
}

void MainWindow::hparse_f_lua_opr_11(int who_is_calling, int allow_flags) /* Precedence = 11 */
{
  hparse_f_lua_opr_12(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "-") == 1) || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "+") == 1))
  {
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_BINARY_PLUS_OR_MINUS;
    hparse_f_lua_opr_12(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_lua_opr_12(int who_is_calling, int allow_flags) /* Precedence = 12 */
{
  if (hparse_errno > 0) return;
  hparse_f_lua_opr_13(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "*") == 1)
   || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "/") == 1)
   || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "%") == 1))
  {
    hparse_f_lua_opr_13(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_lua_opr_13(int who_is_calling, int allow_flags) /* Precedence = 13 */
{
  hparse_f_lua_opr_14(who_is_calling, allow_flags);
}

void MainWindow::hparse_f_lua_opr_14(int who_is_calling, int allow_flags) /* Precedence = 14 */
{
  if ((hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "-") == 1)
   || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "not") == 1)
   || (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "#") == 1))
  {
    hparse_f_lua_opr_15(who_is_calling, 0);
  }
  else hparse_f_lua_opr_15(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
}

void MainWindow::hparse_f_lua_opr_15(int who_is_calling, int allow_flags) /* Precedence = 15 */
{
  hparse_f_lua_opr_16(who_is_calling, allow_flags);
  if (hparse_errno > 0) return;
  while (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "^") == 1)
  {
    hparse_f_lua_opr_16(who_is_calling, 0);
    if (hparse_errno > 0) return;
  }
}

void MainWindow::hparse_f_lua_opr_16(int who_is_calling, int allow_flags) /* Precedence = 16 */
{
  hparse_f_lua_opr_17(who_is_calling, allow_flags);
}

void MainWindow::hparse_f_lua_opr_17(int who_is_calling, int allow_flags) /* Precedence = 17 */
{
  hparse_f_lua_opr_18(who_is_calling, allow_flags);
}

/*
  Final level is operand.
  factor = identifier | number | "(" expression ")" .
*/
void MainWindow::hparse_f_lua_opr_18(int who_is_calling, int allow_flags) /* Precedence = 18, top */
{
  if (hparse_errno > 0) return;
  /* if we get 1 it's var, ok. if we get 2 it's functioncall, ok. */
  int nn= hparse_f_lua_functioncall();
  if (nn > 0) return;
  if (hparse_errno > 0) return;
  QString opd= hparse_token;

  int saved_hparse_i= hparse_i;
  hparse_f_next_nexttoken();
  if (hparse_next_token == "(")
  {
    if ((main_token_flags[hparse_i] & TOKEN_FLAG_IS_FUNCTION) != 0)
    {
      int saved_token= main_token_types[hparse_i];
      if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
      {
        hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[reserved function]");
        if (hparse_errno > 0) return;
      }
      main_token_types[saved_hparse_i]= saved_token;
    }
  }
  if (hparse_f_lua_literal() == 1) return;
  if (hparse_errno > 0) return;
  if (hparse_f_lua_tableconstructor() == 1) return;
  if (hparse_errno > 0) return;
  if (hparse_f_lua_accept_dotted(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "..."))
    return;
  if (hparse_f_accept(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "("))
  {
    if (hparse_errno > 0) return;
    /* if subquery is allowed, check for "(SELECT ...") */
    if ((allow_flags & ALLOW_FLAG_IS_MULTI) != 0)
    {
      int expression_count= 0;
      hparse_f_parenthesized_multi_expression(&expression_count);
    }
    else hparse_f_lua_opr_1(who_is_calling);
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_LUA, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
    return;
  }
  hparse_f_error();
  return;
}
/* tokenize() treats every "." as a separate token, work around that */
int MainWindow::hparse_f_lua_accept_dotted(unsigned short int flag_version, unsigned char reftype, int proposed_type, QString token)
{
  if (hparse_errno > 0) return 0;
  int i= hparse_i;
  int dots;
  if (hparse_text_copy.mid(main_token_offsets[i], 3) == "...") dots= 3;
  else if (hparse_text_copy.mid(main_token_offsets[i], 2) == "..") dots= 2;
  else if (hparse_text_copy.mid(main_token_offsets[i], 1) == ".") dots= 1;
  else dots= 0;
  if (dots == token.length())
  {
    if (dots >= 3) hparse_f_accept(flag_version, reftype,proposed_type, ".");
    if (hparse_errno > 0) return 0;
    if (dots >= 2) hparse_f_accept(flag_version, reftype,proposed_type, ".");
    if (hparse_errno > 0) return 0;
    return (hparse_f_accept(flag_version, reftype,proposed_type, "."));
  }

  /* these 2 lines are duplicated in hparse_f_accept() */
  if (hparse_expected > "") hparse_expected.append(" or ");
  hparse_expected.append(hparse_f_token_to_appendee(token, reftype));
  return 0;
}
#endif

/*
  Called from hparse_f_block() for LEAVE label or ITERATE label.
  We go up in main_token list until we hit the top,
  skipping out-of-scope blocks.
  If we pass a label, accept it, it's a legitimate target.
  Todo: Make sure elsewhere that TOKEN_KEYWORD_END is always legitimate.
  Todo: This fails to match qq with `qq`. Should I strip the ``s?
*/
void MainWindow::hparse_f_labels(int block_top)
{
  int count_of_accepts= 0;
  for (int i= hparse_i - 1; ((i >= 0) && (i >= block_top)); --i)
  {
    if (main_token_types[i] == TOKEN_KEYWORD_END)
    {
      int j= main_token_pointers[i];
      if ((j >= i) || (j < block_top)) break; /* should be an assert */
      i= main_token_pointers[i];
      continue;
    }
    if ((main_token_types[i] == TOKEN_TYPE_IDENTIFIER)
     && (main_token_reftypes[i] == TOKEN_REFTYPE_LABEL_DEFINE))
    {
      QString s= hparse_text_copy.mid(main_token_offsets[i], main_token_lengths[i]);
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER, TOKEN_TYPE_IDENTIFIER, s) == 1) return;
      ++count_of_accepts;
    }
  }
  if (count_of_accepts == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_LABEL_REFER, TOKEN_TYPE_IDENTIFIER, "[identifier]");
  else hparse_f_error();
}

/*
  Called from hparse_f_block() for OPEN or FETCH or CLOSE cursor.
  Search method is similar to the one in hparse_f_labels().
  But maybe it's not error if you can't find cursor definition?
  I forget whether that's somehow possible, so allow it.
*/
void MainWindow::hparse_f_cursors(int block_top)
{
  int count_of_accepts= 0;
  for (int i= hparse_i - 1; ((i >= 0) && (i >= block_top)); --i)
  {
    if (main_token_types[i] == TOKEN_KEYWORD_END)
    {
      int j= main_token_pointers[i];
      if ((j >= i) || (j < block_top)) break; /* should be an assert */
      i= main_token_pointers[i];
      continue;
    }
    if ((main_token_types[i] == TOKEN_TYPE_IDENTIFIER)
     && (main_token_reftypes[i] == TOKEN_REFTYPE_CURSOR_DEFINE))
    {
      QString s= hparse_text_copy.mid(main_token_offsets[i], main_token_lengths[i]);
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CURSOR_REFER, TOKEN_TYPE_IDENTIFIER, s) == 1) return;
      ++count_of_accepts;
    }
  }
  if (count_of_accepts == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CURSOR_REFER, TOKEN_TYPE_IDENTIFIER, "[identifier]");
  else hparse_f_error();
}

/*
  Called from hparse_f_block() for FETCH x cursor INTO variable.
  Also called just to count candidates, in which case is_mandatory=false.
  Search method is similar to the one in hparse_f_labels(),
  but we go as far as statement start rather than block_top,
  because parameter declarations precede block top.
  But maybe it's not error if you can't find cursor definition?
  I forget whether that's somehow possible, so allow it.
  Todo: For a parameter, make sure it's an OUT parameter.
  TODO: Finding variables could be useful in lots more places.
  TODO: Check: what if there are 1000 variables, does anything overflow?
*/
int MainWindow::hparse_f_variables(bool is_mandatory)
{
  int count_of_accepts= 0;
  int candidate_count= 0;
  for (int i= hparse_i - 1; ((i >= 0) && (i >= hparse_i_of_statement)); --i)
  {
    if (main_token_types[i] == TOKEN_KEYWORD_END)
    {
      int j= main_token_pointers[i];
      if ((j >= i) || (j < hparse_i_of_statement)) break; /* should be an assert */
      i= main_token_pointers[i];
      continue;
    }
    if (main_token_types[i] == TOKEN_TYPE_IDENTIFIER)
    {
      if ((main_token_reftypes[i] == TOKEN_REFTYPE_VARIABLE_DEFINE)
       || (main_token_reftypes[i] == TOKEN_REFTYPE_PARAMETER))
      {
        ++candidate_count;
        if (is_mandatory)
        {
          QString s= hparse_text_copy.mid(main_token_offsets[i], main_token_lengths[i]);
          if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE_REFER, TOKEN_TYPE_IDENTIFIER, s) == 1) return candidate_count;
          ++count_of_accepts;
        }
      }
    }
  }
  if (is_mandatory)
  {
    if (count_of_accepts == 0) hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE_REFER, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    else hparse_f_error();
  }
  return candidate_count;
}


/*
  Called from hparse_f_block() for SIGNAL or ... HANDLER FOR condition.
  Search method is similar to the one in hparse_f_labels().
  But maybe it's not error if you can't find condition definition?
  I forget whether that's somehow possible, so allow it.
*/
int MainWindow::hparse_f_conditions(int block_top)
{
  int count_of_accepts= 0;
  for (int i= hparse_i - 1; ((i >= 0) && (i >= block_top)); --i)
  {
    if (main_token_types[i] == TOKEN_KEYWORD_END)
    {
      int j= main_token_pointers[i];
      if ((j >= i) || (j < block_top)) return 0; /* should be an assert */
      i= main_token_pointers[i];
      continue;
    }
    if ((main_token_types[i] == TOKEN_TYPE_IDENTIFIER)
     && (main_token_reftypes[i] == TOKEN_REFTYPE_CONDITION_DEFINE))
    {
      QString s= hparse_text_copy.mid(main_token_offsets[i], main_token_lengths[i]);
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CONDITION_REFER, TOKEN_TYPE_IDENTIFIER, s) == 1) return 1;
      ++count_of_accepts;
    }
  }
  if (count_of_accepts == 0) hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_CONDITION_REFER, TOKEN_TYPE_IDENTIFIER, "[identifier]");
  return 0;
}


/*
  This is the top. This should be the main entry for parsing.
  A user might put more than one statement, or block of statements,
  on the statement widget before asking for execution.
  Re hparse_dbms_mask:
    We do check (though not always and not reliably) whether
    (hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0) before accepting | expecting,
    for example "role" will only be recognized if (hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0.
    Todo: we should check version number too, someday.
    If we are connected, then the SELECT VERSION() result, which we stored in
    statement_edit_widget->dbms_version, will include the string "MariaDB".
    If we are not connected, the default is "mysql" but the user can start with
    ocelotgui --ocelot_dbms=mariadb, and we store that in ocelot_dbms.
*/
void MainWindow::hparse_f_multi_block(QString text)
{
  log("hparse_f_multi_block start", 90);
  hparse_line_edit->hide();
  if (connections_is_connected[0] == 1) hparse_dbms_mask= dbms_version_mask;
  else if (ocelot_dbms == "mariadb") hparse_dbms_mask= FLAG_VERSION_MARIADB_ALL;
  else if (ocelot_dbms == "mysql") hparse_dbms_mask= FLAG_VERSION_MYSQL_ALL;
#ifdef DBMS_TARANTOOL
  else if (ocelot_dbms == "tarantool") hparse_dbms_mask= FLAG_VERSION_TARANTOOL;
#endif
  else hparse_dbms_mask= FLAG_VERSION_MYSQL_OR_MARIADB_ALL;
  hparse_i= -1;
  hparse_delimiter_str= ocelot_delimiter_str;
  for (;;)
  {
    hparse_statement_type= -1;
    hparse_errno= 0;
    hparse_expected= "";
    hparse_text_copy= text;
    hparse_begin_seen= false;
    hparse_like_seen= false;
    hparse_token_type= 0;
    hparse_next_token= "";
    hparse_next_next_token= "";
    hparse_next_next_next_token= "";
    hparse_next_next_next_next_token= "";
    hparse_next_token_type= 0;
    hparse_next_next_token_type= 0;
    hparse_next_next_next_token_type= 0;
    hparse_next_next_next_next_token_type= 0;
    hparse_prev_token= "";
    hparse_subquery_is_allowed= false;
    hparse_count_of_accepts= 0;
    hparse_i_of_last_accepted= 0;
    if (hparse_i == -1) hparse_f_nexttoken();
    hparse_i_of_statement= hparse_i;
    if (hparse_f_client_statement() == 1)
    {
      if (main_token_lengths[hparse_i] == 0) return; /* empty token marks end of input */
      if ((hparse_prev_token != ";") && (hparse_prev_token != hparse_delimiter_str))
      {
        hparse_f_semicolon_and_or_delimiter(0);
        if (hparse_errno > 0) goto error;
        if (main_token_lengths[hparse_i] == 0) return;
      }
      continue; /* ?? rather than "return"? */
    }
    if (hparse_errno > 0) goto error;
#ifdef DBMS_MARIADB
    if ((hparse_dbms_mask & FLAG_VERSION_MARIADB_ALL) != 0)
    {
      hparse_f_block(0, hparse_i);
    }
    else
#endif
    {
#ifdef DBMS_TARANTOOL
      if (((hparse_dbms_mask & FLAG_VERSION_TARANTOOL) != 0)
       && (hparse_f_is_nosql(text) == true))
         tparse_f_block(0);
      else
#endif
      hparse_f_statement(hparse_i);
      if (hparse_errno > 0) goto error;
      /*
        Todo: we had trouble because some functions eat the final semicolon.
        The best thing would be to eat properly. Till then, we'll kludge:
        if we've just seen ";", don't ask for it again.
      */
      if ((hparse_prev_token != ";") && (hparse_prev_token != hparse_delimiter_str))
      {
        if (hparse_f_semicolon_and_or_delimiter(0) != 1) hparse_f_error();
      }
      if (hparse_errno > 0) goto error;
    }
    //hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "[eof]");
    if (hparse_errno > 0) goto error;
    if (hparse_i > 0) main_token_flags[hparse_i - 1]= (main_token_flags[hparse_i - 1] | TOKEN_FLAG_IS_BLOCK_END);
    if (main_token_lengths[hparse_i] == 0) return; /* empty token marks end of input */
  }
  log("hparse_f_multi_block end", 90);
  return;
error:
  log("hparse_f_multi_block error", 90);
  QString expected_list;
  bool unfinished_comment_seen= false;
  bool unfinished_identifier_seen= false;
  if ((hparse_i == 0) && (main_token_lengths[0] == 0)) return;
  /* Do not add to expecteds if we're still inside a comment */
  if ((hparse_i > 0) && (main_token_lengths[hparse_i] == 0))
  {
    int j= hparse_i - 1;
    if (main_token_types[j] == TOKEN_TYPE_COMMENT_WITH_SLASH)
    {
      QString token= hparse_text_copy.mid(main_token_offsets[j], main_token_lengths[j]);
      if (token.right(2) != "*/")
      {
        unfinished_comment_seen= true;
        if ((token.right(1) == "*") && (token != "/*")) expected_list= "Expecting: /";
        else expected_list= "Expecting: */";
      }
    }
    if ((main_token_types[j] == TOKEN_TYPE_COMMENT_WITH_MINUS)
     || (main_token_types[j] == TOKEN_TYPE_COMMENT_WITH_OCTOTHORPE))
    {
      QString rest= hparse_text_copy.mid(main_token_offsets[j]);
      if (rest.contains("\n") == false) return;
    }
  }
  /* Add different set of expecteds if we're still inside a quoted identifier */
  if ((main_token_types[hparse_i] == TOKEN_TYPE_IDENTIFIER_WITH_BACKTICK)
   || (main_token_types[hparse_i] == TOKEN_TYPE_IDENTIFIER_WITH_DOUBLE_QUOTE))
  {
    QString token= hparse_text_copy.mid(main_token_offsets[hparse_i], main_token_lengths[hparse_i]);
    bool missing_termination= false;
    if (token.size() == 1) missing_termination= true;
    else if ((token.left(1) == "\"") && (token.right(1) != "\"")) missing_termination= true;
    else if ((token.left(1) == "`") && (token.right(1) != "`")) missing_termination= true;
    if (missing_termination == true)
    {
      if (ocelot_auto_rehash > 0)
      {
        QString errmsg= hparse_errmsg;
        if (errmsg.contains("identifier]") == true)
        {
          QString s;
          char search_string[512];
          int search_string_len;
          s= text.mid(main_token_offsets[hparse_i], main_token_lengths[hparse_i]);
          if (s.left(1) == "`") s= s.right(s.size() - 1);
          else if (s.left(1) == "\"") s= s.right(s.size() - 1);

          search_string_len= s.toUtf8().size();
          memcpy(search_string, s.toUtf8().constData(), search_string_len);
          search_string[search_string_len]= '\0';
          QString rehash_search_result= rehash_search(search_string, main_token_reftypes[hparse_i]);
          if (rehash_search_result > "")
          {
            expected_list= "Expecting: ";
            expected_list.append(rehash_search_result);
            unfinished_identifier_seen= true;
          }
        }
      }
    }
  }

  if ((unfinished_comment_seen == false) && (unfinished_identifier_seen == false))
  {
    expected_list= "Expecting: ";
    QString s_token;
    QString errmsg= hparse_errmsg;
    int word_start= errmsg.indexOf("tokens is: ") + 11;
    int word_end;
    for (;;)
    {
      word_end= errmsg.indexOf(" ", word_start);
      if (word_end == -1) word_end= errmsg.size();
      s_token= errmsg.mid(word_start, word_end - word_start);
      s_token= connect_stripper(s_token, false);
      if (s_token != "or")
      {
        if ((s_token.left(1) == "[")
         || (QString::compare(hparse_token, s_token.left(hparse_token.size()), Qt::CaseInsensitive) == 0))
        {
          /* This bit is to prevent saying the same token twice */
          QString s_token_2= s_token;
          s_token_2.append(" ");
          QString expected_list_2= expected_list.right(expected_list.size() - 11);
          if (expected_list_2.contains(s_token_2, Qt::CaseInsensitive) == false)
          {
            expected_list.append(s_token);
            expected_list.append(" ");
          }
        }
      }
      word_start= word_end + 1;
      if (word_start >= errmsg.size()) break;
    }
  }

  hparse_line_edit->setText(expected_list);
  hparse_line_edit->setCursorPosition(0);
  hparse_line_edit->show();
  log("hparse_f_multi_block end", 90);
}

#ifdef DBMS_TARANTOOL
bool MainWindow::hparse_f_is_nosql(QString text)
{
  QString s= text.mid(main_token_offsets[hparse_i], main_token_lengths[hparse_i]);
  if ((QString::compare(s, "SELECT", Qt::CaseInsensitive) == 0)
   || (QString::compare(s, "INSERT", Qt::CaseInsensitive) == 0)
   || (QString::compare(s, "DELETE", Qt::CaseInsensitive) == 0))
  {
    QString s= text.mid(main_token_offsets[hparse_i + 1], main_token_lengths[hparse_i + 1]);
    if ((s.left(2) == "/*") && (s.right(2) == "*/"))
    {
      s= s.mid(2, s.length() - 4).trimmed();
      if (QString::compare(s, "NOSQL", Qt::CaseInsensitive) == 0)
      {
        return true;
      }
    }
  }
  return false;
}
#endif

/*
  A client statement can be "\" followed by a character, for example \C.
  Todo: The tokenizer must find these combinations and produce two tokens: \ and C.
        It does not matter what follows the C, it is considered to be a breaker.
        Even statements like "drop table e\Gdrop table y;" are supposed to be legal.
  Case sensitive.
  Checking for the first word of a client statement.
  \? etc. are problems because (a) they're two tokens, (b) they're case sensitive (c) they delimit.
  \ is a statement-end, and C is a statement.
  For highlighting, the \ is an operator and the C is a keyword.
  So this could actually be called from hparse_f_semicolon_or_delimiter.
  This routine does not accept and go ahead, it just tells us that's what we're looking at.
  These tokens will not show up in a list of predictions.
*/
int MainWindow::hparse_f_backslash_command(bool eat_it)
{
  int slash_token= -1;
  if (hparse_token != "\\") return 0;
  if (main_token_lengths[hparse_i + 1] != 1) return 0;
  QString s= hparse_text_copy.mid(main_token_offsets[hparse_i + 1], 1);
  if (s == QString("?")) slash_token= TOKEN_KEYWORD_QUESTIONMARK;
  else if (s == QString("C")) slash_token= TOKEN_KEYWORD_CHARSET;
  else if (s == QString("c")) slash_token= TOKEN_KEYWORD_CLEAR;
  else if (s == QString("r")) slash_token= TOKEN_KEYWORD_CONNECT;
  else if (s == QString("d")) slash_token= TOKEN_KEYWORD_DELIMITER;
  else if (s == QString("e")) slash_token= TOKEN_KEYWORD_EDIT;
  else if (s == QString("G")) slash_token= TOKEN_KEYWORD_EGO;
  else if (s == QString("g")) slash_token= TOKEN_KEYWORD_GO;
  else if (s == QString("h")) slash_token= TOKEN_KEYWORD_HELP;
  else if (s == QString("n")) slash_token= TOKEN_KEYWORD_NOPAGER;
  else if (s == QString("t")) slash_token= TOKEN_KEYWORD_NOTEE;
  else if (s == QString("w")) slash_token= TOKEN_KEYWORD_NOWARNING;
  else if (s == QString("P")) slash_token= TOKEN_KEYWORD_PAGER;
  else if (s == QString("p")) slash_token= TOKEN_KEYWORD_PRINT;
  else if (s == QString("R")) slash_token= TOKEN_KEYWORD_PROMPT;
  else if (s == QString("q")) slash_token= TOKEN_KEYWORD_QUIT;
  else if (s == QString("#")) slash_token= TOKEN_KEYWORD_REHASH;
  else if (s == QString(".")) slash_token= TOKEN_KEYWORD_SOURCE;
  else if (s == QString("s")) slash_token= TOKEN_KEYWORD_STATUS;
  else if (s == QString("!")) slash_token= TOKEN_KEYWORD_SYSTEM;
  else if (s == QString("T")) slash_token= TOKEN_KEYWORD_TEE;
  else if (s == QString("u")) slash_token= TOKEN_KEYWORD_USE;
  else if (s == QString("W")) slash_token= TOKEN_KEYWORD_WARNINGS;
  //else if (s == QString("x")) slash_token= TOKEN_KEYWORD_TOKEN_KEYWORD_RESETCONNECTION;
  else return 0;
  if (eat_it == true)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,slash_token, "\\"); /* Todo: mark as TOKEN_FLAG_END */
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,slash_token, s);
    if (hparse_errno > 0) return 0;
  }
  return slash_token;
}

/*
  Certain client statements -- delimiter, prompt, source -- take pretty well anything
  as the end of the line. So we want the highlight to always be the same
  (we picked "literal" but anything would do). Some deviations from mysql client:
  * we allow SOURCE 'literal' etc., anything within the quote marks is the argument
  * we allow comments, we do not consider them to be part of the argument
  * we haven't figured out what to do with delimiters or ;
  * it's uncertain what we'll do when it comes time to execute
  * delimiter can end with space, but source and prompt cannot, that's not handled
  * todo: this isn't being called for prompt
  Delimiters
  ----------
  If DELIMITER is the only or the first statement, rules are documented and comprehensible:
    Whatever follows \d or "delimiter" is a single token as far as " ",
    or a quoted string quoted by ' or " or `. So we change tokenizer() to say:
    * first check for quoted string (if it is, then token #1 is quoted string)
    * if it's start of token#1, and token#0 is \d or "delimiter", skip till " " or <eof>
    The result is in effect for the next tokenize, not for subsequent statements on the line.
  If DELIMITER is not the first statement, rules are not documented and bizarre:
    The string that follows is the new delimiter, but the rest of the line is ignored.
  DELIMITER causes new rules! Everything following as far as " " is delimiter-string.
*/
/* flag values: 1 means "; marks end" */
void MainWindow::hparse_f_other(int flags)
{
  if ((main_token_types[hparse_i] == TOKEN_TYPE_LITERAL_WITH_SINGLE_QUOTE)
   || (main_token_types[hparse_i] == TOKEN_TYPE_LITERAL_WITH_DOUBLE_QUOTE))
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]"); /* guaranteed to succeed */
    if (hparse_errno > 0) return;
  }
  else if (main_token_types[hparse_i] == TOKEN_TYPE_IDENTIFIER_WITH_BACKTICK)
  {
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "[identifier]"); /* guaranteed to succeed */
    if (hparse_errno > 0) return;
  }
  else
  {
    if (main_token_lengths[hparse_i] == 0)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]"); /* guaranteed to fail */
      if (hparse_errno > 0) return;
    }
  }
  for (;;)
  {
    if (main_token_lengths[hparse_i] == 0) break;
    if (((flags&1) == 1) && (main_token_lengths[hparse_i] == 1))
    {
      if (hparse_text_copy.mid(main_token_offsets[hparse_i], 1) == ";")
      {
        hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ";");
        break;
      }
    }
    main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
    main_token_types[hparse_i]= hparse_token_type= TOKEN_TYPE_LITERAL;
    //if (main_token_lengths[hparse_i + 1] == 0)
    //{
    //  break;
    //}
    bool line_break_seen= false;
    for (int i_off= main_token_offsets[hparse_i] + main_token_lengths[hparse_i];; ++i_off)
    {
      if (i_off >= main_token_offsets[hparse_i + 1]) break;
      QString q= hparse_text_copy.mid(i_off, 1);
      if ((q == "\n") || (q == "\r"))
      {
        line_break_seen= true;
        break;
      }
    }
    if (line_break_seen == true)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]"); /* guaranteed to succeed */
      if (hparse_errno > 0) return;
      break;
    }

    //if (main_token_offsets[hparse_i] + main_token_lengths[hparse_i]
    //   < main_token_offsets[hparse_i + 1])
    //{
    //  break;
    //}
    hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]"); /* guaranteed to succeed */
    if (hparse_errno > 0) return;
  }
}

/*
  Statements handled locally (by ocelotgui), which won't go to the server.
  Todo: we're ignoring --binary-mode.
  Todo: we're only parsing the first word to see if it's client-side, we could do more.
  SET is a special problem because it can be either client or server (flaw in our design?).
  Within client statements, reserved words don't count so we turn the reserved flag off.
  Todo: Figure out how HELP can be both client statement and server statement.
*/
int MainWindow::hparse_f_client_statement()
{
  hparse_next_token= hparse_next_next_token= "";
  hparse_next_next_next_token= hparse_next_next_next_next_token= "";
  int saved_hparse_i= hparse_i;
  int saved_hparse_token_type= hparse_token_type;
  QString saved_hparse_token= hparse_token;
  int slash_token= hparse_f_backslash_command(true);
  if (hparse_errno > 0) return 0;
  if ((slash_token == TOKEN_KEYWORD_QUESTIONMARK) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_QUESTIONMARK, "?") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_CHARSET) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CHARSET, "CHARSET") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_CHARACTER_SET,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
    }
  }
  else if ((slash_token == TOKEN_KEYWORD_CLEAR) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CLEAR, "CLEAR") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_CONNECT) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_CONNECT, "CONNECT") == 1))
  {
     if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_DELIMITER) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_DELIMITER, "DELIMITER") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    QString tmp_delimiter= get_delimiter(hparse_token, hparse_text_copy, main_token_offsets[hparse_i]);
    if (tmp_delimiter > " ")
    {
      hparse_delimiter_str= ";";
      hparse_f_other(1);
      hparse_delimiter_str= tmp_delimiter;
      /* Redo tokenize because if delimiter changes then token ends change. */
      if ((main_token_lengths[hparse_i] != 0) && (main_token_offsets[hparse_i] != 0))
      {
        int offset_of_rest= main_token_offsets[hparse_i];
        tokenize(hparse_text_copy.data() + offset_of_rest,
                 hparse_text_copy.size() - offset_of_rest,
                 main_token_lengths + hparse_i,
                 main_token_offsets + hparse_i,
                 main_token_max_count - (hparse_i + 1),
                 (QChar*)"33333",
                 1,
                 hparse_delimiter_str,
                 1);
        for (int ix= hparse_i; main_token_lengths[ix] != 0; ++ix)
        {
          main_token_offsets[ix]+= offset_of_rest;
        }
        tokens_to_keywords(hparse_text_copy, hparse_i);
      }
    }
    else hparse_f_other(1);
    if (hparse_errno > 0) return 0;
  }
  else if ((slash_token == TOKEN_KEYWORD_EDIT) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EDIT, "EDIT") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_EGO) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EGO, "EGO") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_EXIT) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_EXIT, "EXIT") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_GO) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_GO, "GO") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_HELP) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_HELP, "HELP") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_NOPAGER) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_NOPAGER, "NOPAGER") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_NOTEE) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_NOTEE, "NOTEE") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_NOWARNING) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_NOWARNING, "NOWARNING") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_PAGER) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PAGER, "PAGER") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_PRINT) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PRINT, "PRINT") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_PROMPT) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_PROMPT, "PROMPT")== 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    /* PROMPT can be followed by any bunch of junk as far as ; or delimiter or eof or \n*/
    QString d;
    int j;
    for (;;)
    {
      j= main_token_offsets[hparse_i - 1] + main_token_lengths[hparse_i - 1];
      d= hparse_text_copy.mid(j, main_token_offsets[hparse_i]- j);
      if (d.contains("\n")) break;
      if ((main_token_lengths[hparse_i] == 0)
       //|| (hparse_token == ";")
       || (hparse_token == hparse_delimiter_str)) break;
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
      main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
      main_token_types[hparse_i]= TOKEN_TYPE_OTHER;
      hparse_f_nexttoken();
    }
  }
  else if ((slash_token == TOKEN_KEYWORD_QUIT) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_QUIT, "QUIT") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_REHASH) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_REHASH, "REHASH") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  //else if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_KEYWORD_RESETCONNECTION, "RESETCONNECTION") == 1))
  //{
  // if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  //}
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SET, "SET") == 1)
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (main_token_lengths[hparse_i] != 0)
    {
      QString s= hparse_token.mid(0, 7);
      if (QString::compare(s, "OCELOT_", Qt::CaseInsensitive) != 0)
      {
        hparse_i= saved_hparse_i;
        hparse_token_type= saved_hparse_token_type;
        hparse_token= saved_hparse_token;
        return 0;
      }
    }
    if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_TEXT_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_BORDER_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FONT_FAMILY") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FONT_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FONT_STYLE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FONT_WEIGHT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_LITERAL_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_IDENTIFIER_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_COMMENT_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_OPERATOR_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_KEYWORD_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_PROMPT_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_CURRENT_LINE_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_HIGHLIGHT_FUNCTION_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_SYNTAX_CHECKER") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FORMAT_STATEMENT_INDENT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FORMAT_CLAUSE_INDENT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_STATEMENT_FORMAT_KEYWORD_CASE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_TEXT_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_HEADER_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_BORDER_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_FONT_FAMILY") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_FONT_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_FONT_STYLE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_FONT_WEIGHT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_CELL_BORDER_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_CELL_DRAG_LINE_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_BORDER_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_CELL_BORDER_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_GRID_CELL_DRAG_LINE_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_EXTRA_RULE_1_TEXT_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_EXTRA_RULE_1_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_EXTRA_RULE_1_CONDITION") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_EXTRA_RULE_1_DISPLAY_AS") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_TEXT_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_BORDER_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_FONT_FAMILY") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_FONT_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_FONT_STYLE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_FONT_WEIGHT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HISTORY_MAX_ROW_COUNT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_MENU_TEXT_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_MENU_BACKGROUND_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_MENU_BORDER_COLOR") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_FONT_FAMILY") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_MENU_FONT_SIZE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_MENU_FONT_STYLE") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_MENU_FONT_WEIGHT") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_BATCH") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_HTML") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_RAW") == 1)
     || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_IDENTIFIER, "OCELOT_XML") == 1))
    {
      ;
    }
    else hparse_f_error();
    if (hparse_errno > 0) return 0;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
    if (hparse_errno > 0) return 0;
    main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_RESERVED);
    main_token_flags[hparse_i] &= (~TOKEN_FLAG_IS_FUNCTION);
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
    if (hparse_errno > 0) return 0;
  }
  else if ((slash_token == TOKEN_KEYWORD_SOURCE) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SOURCE, "SOURCE") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_other(0);
    if (hparse_errno > 0) return 0;
  }
  else if ((slash_token == TOKEN_KEYWORD_STATUS) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_STATUS, "STATUS") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((slash_token == TOKEN_KEYWORD_SYSTEM) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_SYSTEM, "SYSTEM") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_other(1);
    if (hparse_errno > 0) return 0;
  }
  else if ((slash_token == TOKEN_KEYWORD_TEE) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_TEE, "TEE") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    hparse_f_other(1);
    if (hparse_errno > 0) return 0;
  }
  else if ((slash_token == TOKEN_KEYWORD_USE) || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_USE, "USE") == 1))
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
    if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_DATABASE,TOKEN_TYPE_IDENTIFIER, "[identifier]") == 0)
    {
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
    }
    /* In mysql client, garbage can follow. It's not documented so don't call hparse_f_other(). */
    if (hparse_errno > 0) return 0;
  }
  else if ((slash_token == TOKEN_KEYWORD_WARNINGS) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY,TOKEN_KEYWORD_WARNINGS, "WARNINGS")) == 1)
  {
    if (slash_token <= 0) main_token_flags[hparse_i_of_last_accepted] |= TOKEN_FLAG_IS_START_STATEMENT;
  }
  else if ((hparse_token.mid(0, 1) == "$")
    && ((hparse_dbms_mask & FLAG_VERSION_MYSQL_OR_MARIADB_ALL) != 0))
  {
    main_token_flags[hparse_i] |= TOKEN_FLAG_IS_START_STATEMENT;
    /* TODO: We aren't parsing $debug statements well */
    if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_BREAKPOINT, "$BREAKPOINT", 2) == 1)
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION_OR_PROCEDURE, TOKEN_REFTYPE_FUNCTION_OR_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
      if ((hparse_token.length() == 0) || (hparse_token == ";")) return 1;
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return 0;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_CLEAR, "$CLEAR", 3) == 1)
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION_OR_PROCEDURE, TOKEN_REFTYPE_FUNCTION_OR_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_CONTINUE, "$CONTINUE", 3) == 1)
    {
      ;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_DEBUG, "$DEBUG", 4) == 1)
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION_OR_PROCEDURE, TOKEN_REFTYPE_FUNCTION_OR_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "(") == 1)
      {
        if (hparse_token != ")")
        {
          do
          {
            if (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]") == 0)
            {
              hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
            }
            if (hparse_errno > 0) return 0;
          } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
        }
      }
    }
     //|| (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_DELETE, "$DELETE") == 1)
     //|| (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_EXECUTE, "$EXECUTE") == 1)
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_EXIT, "$EXIT", 4) == 1)
    {
      ;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_INFORMATION, "$INFORMATION", 4) == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "STATUS");
      if (hparse_errno > 0) return 0;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_INSTALL, "$INSTALL", 4) == 1)
    {
      ;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_LEAVE, "$LEAVE", 2) == 1)
    {
      ;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_NEXT, "$NEXT", 2) == 1)
    {
      ;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_REFRESH, "$REFRESH", 8) == 1)
    {
      if ((hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "BREAKPOINTS") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "CALL_STACK") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "SERVER_VARIABLES") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "USER_VARIABLES") == 1)
       || (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_KEYWORD, "VARIABLES") == 1))
        {;}
      else hparse_f_error();
      if (hparse_errno > 0) return 0;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_SET, "$SET", 4) == 1)
    {
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_VARIABLE,TOKEN_TYPE_IDENTIFIER, "[identifier]");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, "=");
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_SETUP, "$SETUP", 5) == 1)
    {
      do
      {
        if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION_OR_PROCEDURE, TOKEN_REFTYPE_FUNCTION_OR_PROCEDURE) == 0) hparse_f_error();
        if (hparse_errno > 0) return 0;
      } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_OPERATOR, ","));
    }
     //|| (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_SKIP, "$SKIP") == 1)
     //|| (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_SOURCE, "$SOURCE") == 1)
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_STEP, "$STEP", 3) == 1)
    {
      ;
    }
    else if (hparse_f_acceptn(TOKEN_KEYWORD_DEBUG_TBREAKPOINT, "$TBREAKPOINT", 2) == 1)
    {
      if (hparse_f_qualified_name_of_object(TOKEN_REFTYPE_DATABASE_OR_FUNCTION_OR_PROCEDURE, TOKEN_REFTYPE_FUNCTION_OR_PROCEDURE) == 0) hparse_f_error();
      if (hparse_errno > 0) return 0;
      hparse_f_expect(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY,TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return 0;
      if ((hparse_token.length() == 0) || (hparse_token == ";")) return 1;
      hparse_f_opr_1(0);
      if (hparse_errno > 0) return 0;
    }
    else return 0;
    return 1;
  }
  else
  {
    return 0;
  }
  return 1;
}

/*
  The hint line that appears underneath the statement widget if syntax error,
  which probably means that the user is typing and hasn't finished a word.
  This is somewhat like a popup but using "Qt::Popup" caused trouble.
*/
void MainWindow::hparse_f_parse_hint_line_create()
{
  hparse_line_edit= new QLineEdit(this);
  hparse_line_edit->setReadOnly(true);
  hparse_line_edit->hide();
}

#ifdef DBMS_TARANTOOL
/*
  See ocelotgui.h comments under heading "Tarantool comments".

  Syntax check (!! THIS COMMENT MAY BE OBSOLETE!)
  Use the recognizer for a small subset of MySQL/MariaDB syntax ...
  DELETE FROM identifier WHERE identifier = literal [AND identifier = literal ...];
  INSERT INTO identifier VALUES (literal [, literal...]);
  REPLACE INTO identifier VALUES (literal [, literal...]);
  SELECT * FROM identifier [WHERE identifier <comparison-operator> literal [AND identifier <comparison-operator> literal ...]];
  UPDATE identifier SET identifier=value [, identifier=value...]
                    WHERE identifier = literal [AND identifier = literal ...];
  SET identifier = expression [, identifier = expression ...]
  Legal comparison-operators within SELECT are = > < >= <=
  Comments are legal anywhere.
  Todo: Keywords should not be reserved, for example DELETE FROM INTO WHERE SELECT=5; is legal.
  We call tparse_f_block(0) for "SELECT|INSERT|DELETE / * NOSQL * / ..."
  Todo: with the current arrangement we never could reach "TRUNCATE".
  Todo: with the current arrangement we never could reach "SET".
  Todo: UPDATE fails.
*/

/* These items are permanent and are initialized in parse_f_program */
static int tparse_iterator_type= TARANTOOL_BOX_INDEX_EQ;
static int tparse_indexed_condition_count= 0;

/*
 factor = identifier | literal | "(" expression ")" .
*/
void MainWindow::tparse_f_factor()
{
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN, TOKEN_TYPE_IDENTIFIER, "[identifier]"))
  {
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_LITERAL, "[literal]"))
  {
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "("))
  {
    if (hparse_errno > 0) return;
    tparse_f_expression();
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
  }
  else
  {
    hparse_f_error();
    return;
  }
}

/*
  term = factor {("*"|"/") factor}
*/
void MainWindow::tparse_f_term()
{
  if (hparse_errno > 0) return;
  tparse_f_factor();
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "*") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "/") == 1))
  {
    tparse_f_factor();
    if (hparse_errno > 0) return;
  }
}

/*
   expression = ["+"|"-"] term {("+"|"-") term}
*/
void MainWindow::tparse_f_expression()
{
  if (hparse_errno > 0) return;
  if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "+") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "-") == 1)) {;}
  tparse_f_term();
  if (hparse_errno > 0) return;
  while ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "+") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "-") == 1))
  {
    tparse_f_term();
    if (hparse_errno > 0) return;
  }
}

/*
  restricted expression = ["+"|"-"] literal
*/
void MainWindow::tparse_f_restricted_expression()
{
  if (hparse_errno > 0) return;
  if ((hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "+") == 1) || (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "-") == 1)) {;}
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_LITERAL, "[literal]");
}

/*
 condition =
     identifier ("="|"<"|"<="|"="|">"|">=") literal
     [AND condition ...]
*/
void MainWindow::tparse_f_indexed_condition(int keyword)
{
  if (hparse_errno > 0) return;
  do
  {
    if (tparse_indexed_condition_count >= 255)
    {
      hparse_expected= "no more conditions. The maximum is 255 (box.schema.INDEX_PART_MAX).";
      hparse_f_error();
      return;
    }
    int comp_op= -1;
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_COLUMN, TOKEN_TYPE_IDENTIFIER, "[field identifier]") == 0)
    {
      hparse_expected= "field identifier with the format: ";
      hparse_expected.append(TARANTOOL_FIELD_NAME_BASE);
      hparse_expected.append("_ followed by an integer greater than zero. ");
      hparse_expected.append("Maximum length = 64. ");
      hparse_expected.append(QString::number(TARANTOOL_MAX_FIELD_NAME_LENGTH));
      hparse_expected.append(". For example: ");
      hparse_expected.append(TARANTOOL_FIELD_NAME_BASE);
      hparse_expected.append("_1");
      hparse_f_error();
      return;
    }

    if (tparse_indexed_condition_count > 0)
    {
      int ok= 0;
      if ((hparse_token == "<")
       && (tparse_iterator_type == TARANTOOL_BOX_INDEX_LE)
       && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "<") == 1)) {;}
      else if ((hparse_token == ">")
            && (tparse_iterator_type == TARANTOOL_BOX_INDEX_GE)
            && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ">") == 1)) ++ok;
      else if ((hparse_token == "=")
            && (tparse_iterator_type == TARANTOOL_BOX_INDEX_EQ)
            && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=") == 1)) ++ok;
      else if ((hparse_token == "<=")
            && (tparse_iterator_type == TARANTOOL_BOX_INDEX_LE)
            && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "<=") == 1)) ++ok;
      else if ((hparse_token == ">=")
            && (tparse_iterator_type == TARANTOOL_BOX_INDEX_GE)
            && (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ">=") == 1)) ++ok;
      if (ok == 0)
      {
        hparse_expected= "A conditional operator compatible with previous ones. ";
        hparse_expected.append("When there is more than one ANDed condition, ");
        hparse_expected.append("allowed combinations are: > after a series of >=s, ");
        hparse_expected.append("or < after a series of <=s, or all =s, or all >=s, or all <=s.");
        hparse_f_error();
        return;
      }
    }
    else
    {
      if (keyword == TOKEN_KEYWORD_SELECT)
      {
        if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=") == 1) comp_op= TARANTOOL_BOX_INDEX_EQ;
        else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "<") == 1) comp_op= TARANTOOL_BOX_INDEX_LT;
        else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "<=") == 1) comp_op= TARANTOOL_BOX_INDEX_LE;
        else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=") == 1) comp_op= TARANTOOL_BOX_INDEX_EQ;
        else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ">") == 1) comp_op= TARANTOOL_BOX_INDEX_GT;
        else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ">=") == 1) comp_op= TARANTOOL_BOX_INDEX_GE;
        else hparse_f_error();
        if (hparse_errno > 0) return;
      }
      else
      {
        hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=");
        if (hparse_errno > 0) return;
        comp_op= TARANTOOL_BOX_INDEX_EQ;
      }
    }
    tparse_iterator_type= comp_op;
    ++tparse_indexed_condition_count;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_LITERAL, "[literal]");
    if (hparse_errno > 0) return;
  } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "AND"));
}


/*
 unindexed condition =
     expression ("="|"<"|"<="|"="|">"|">=") expression
     [AND|OR condition ...]
  For a sequential search, i.e. a full-table scan or a filter of the
  rows selected by indexed conditions, we can have OR as well as AND,
  expressions as well as identifiers, expressions as well as literals,
  and <> as well as other comp-ops.
  May implement for a HAVING clause somday.
*/
//void MainWindow::tparse_f_unindexed_condition()
//{
//  if (hparse_errno >0) return;
//  do
//  {
//    if (hparse_errno > 0) return;
//    tparse_f_expression();
//    if (hparse_errno > 0) return;
//    /* TODO: THIS IS NOWHERE NEAR CORRECT! THERE MIGHT BE MORE THAN ONE OPERAND! */
//    {
//      if ((hparse_token == "=")
//      || (hparse_token == "<>")
//      || (hparse_token == "<")
//      || (hparse_token == "<=")
//      || (hparse_token == ">")
//      || (hparse_token == ">="))
//      {
//        if (hparse_token == "=") tparse_iterator_type= TARANTOOL_BOX_INDEX_EQ;
//        if (hparse_token == "<>") tparse_iterator_type= TARANTOOL_BOX_INDEX_ALL; /* TODO: NO SUPPORT */
//        if (hparse_token == "<") tparse_iterator_type= TARANTOOL_BOX_INDEX_LT;
//        if (hparse_token == "<=") tparse_iterator_type= TARANTOOL_BOX_INDEX_LE;
//        if (hparse_token == ">") tparse_iterator_type= TARANTOOL_BOX_INDEX_GT;
//        if (hparse_token == ">=") tparse_iterator_type= TARANTOOL_BOX_INDEX_GE;
//        hparse_f_nexttoken();
//        parse_f_expression();
//      }
//      else hparse_f_error();
//    }
//  } while (hparse_f_accept(FLAG_VERSION_MYSQL_OR_MARIADB_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_IDENTIFIER, "AND"));
//}


void MainWindow::tparse_f_assignment()
{
  do
  {
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_VARIABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "=");
    if (hparse_errno > 0) return;
    tparse_f_expression();
    if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ","));
}

/*
statement =
    "insert" [into] ident "values" (literal {"," literal} )
    | "replace" [into] ident "values" (number {"," literal} )
    | "delete" "from" ident "where" condition [AND condition ...]
    | "select" * "from" ident ["where" condition [AND condition ...]]
    | "set" ident = number [, ident = expression ...]
    | "truncate" "table" ident
    | "update" ident "set" ident=literal {"," ident=literal} WHERE condition [AND condition ...]
*/
void MainWindow::tparse_f_statement()
{
  if (hparse_errno > 0) return;
  if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "INSERT"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_INSERT;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "INTO");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "VALUES");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(");
    do
    {
      if (hparse_errno > 0) return;
      tparse_f_restricted_expression();
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ","));
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ")");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "REPLACE"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_REPLACE;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "INTO");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "VALUES");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(");
    do
    {
      if (hparse_errno > 0) return;
      hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_LITERAL, "[literal]");
      if (hparse_errno > 0) return;
    } while (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ","));
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "(");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "DELETE"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_DELETE;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "FROM");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "WHERE");
    if (hparse_errno > 0) return;
    tparse_f_indexed_condition(TOKEN_KEYWORD_DELETE);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "TRUNCATE"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_TRUNCATE;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "TABLE");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "SELECT"))
  {
    if (hparse_errno > 0) return;
    hparse_statement_type= TOKEN_KEYWORD_SELECT;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "*");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "FROM");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "WHERE"))
    {
      tparse_f_indexed_condition(TOKEN_KEYWORD_SELECT);
      if (hparse_errno > 0) return;
    }
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "UPDATE"))
  {
    hparse_statement_type= TOKEN_KEYWORD_UPDATE;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_TABLE, TOKEN_TYPE_IDENTIFIER, "[identifier]");
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "SET");
    if (hparse_errno > 0) return;
    tparse_f_assignment();
    if (hparse_errno > 0) return;
    hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "WHERE");
    if (hparse_errno > 0) return;
    tparse_f_indexed_condition(TOKEN_KEYWORD_UPDATE);
    if (hparse_errno > 0) return;
  }
  else if (hparse_f_accept(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_KEYWORD, "SET"))
  {
    hparse_statement_type= TOKEN_KEYWORD_SET;
    tparse_f_assignment();
    if (hparse_errno > 0) return;
  }
  else
  {
    hparse_f_error();
  }
}

/*
  statement
*/
void MainWindow::tparse_f_block(int calling_type)
{
  (void) calling_type; /* to avoid "unused parameter" warning */
  if (hparse_errno > 0) return;
  tparse_iterator_type= TARANTOOL_BOX_INDEX_EQ;
  tparse_indexed_condition_count= 0;
  tparse_f_statement();
}

void MainWindow::tparse_f_program(QString text)
{
  //tarantool_errno[connection_number]= 0; /* unnecessary, I think */

  hparse_text_copy= text;
  hparse_token= "";
  hparse_i= -1;
  hparse_expected= "";
  hparse_errno= 0;
  hparse_token_type= 0;
  hparse_statement_type= -1;
  hparse_f_nexttoken();
  tparse_f_block(0);
  if (hparse_errno > 0) return;
  /* If you've got a bloody semicolon that's okay too */
  if (hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, ";")) return;
  hparse_f_expect(FLAG_VERSION_ALL, TOKEN_REFTYPE_ANY, TOKEN_TYPE_OPERATOR, "[eof]"); /* was: parse_expect(TOKEN_KEYWORD_PERIOD); */
  if (hparse_errno > 0) return;
}
#endif
