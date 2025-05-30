---
description: 'The loop table function in ClickHouse is used to return query results
  in an infinite loop.'
slug: /sql-reference/table-functions/loop
title: 'loop'
---

# loop Table Function

## Syntax {#syntax}

```sql
SELECT ... FROM loop(database, table);
SELECT ... FROM loop(database.table);
SELECT ... FROM loop(table);
SELECT ... FROM loop(other_table_function(...));
```

## Arguments {#arguments}

| Argument                    | Description                                                                                                          |
|-----------------------------|----------------------------------------------------------------------------------------------------------------------|
| `database`                  | database name.                                                                                                       |
| `table`                     | table name.                                                                                                          |
| `other_table_function(...)` | other table function. Example: `SELECT * FROM loop(numbers(10));` `other_table_function(...)` here is `numbers(10)`. |

## Returned values {#returned_values}

Infinite loop to return query results.

## Examples {#examples}

Selecting data from ClickHouse:

```sql
SELECT * FROM loop(test_database, test_table);
SELECT * FROM loop(test_database.test_table);
SELECT * FROM loop(test_table);
```

Or using other table functions:

```sql
SELECT * FROM loop(numbers(3)) LIMIT 7;
   ┌─number─┐
1. │      0 │
2. │      1 │
3. │      2 │
   └────────┘
   ┌─number─┐
4. │      0 │
5. │      1 │
6. │      2 │
   └────────┘
   ┌─number─┐
7. │      0 │
   └────────┘
``` 
```sql
SELECT * FROM loop(mysql('localhost:3306', 'test', 'test', 'user', 'password'));
...
```