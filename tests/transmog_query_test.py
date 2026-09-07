"""Exercise the exporter's actual equipment SELECTs without a live MySQL server.

SQLite supports these plain SELECT/JOIN statements. This is not a worldserver
integration test or a substitute for deployment verification.
"""
from pathlib import Path
import re
import sqlite3
import unittest

SOURCE = (Path(__file__).parents[1] / 'src/mod_realm_armory.cpp').read_text()
block = SOURCE.split('QueryResult result = transmogSupported ?', 1)[1].split('if (!result)', 1)[0]
queries = ["".join(re.findall(r'"([^"\n]*)"', part)).replace('{}', '1004')
           for part in block.split(': CharacterDatabase.Query(')]

class TransmogQueries(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(':memory:')
        self.db.executescript('''
            CREATE TABLE character_inventory (guid INTEGER, bag INTEGER, slot INTEGER, item INTEGER);
            CREATE TABLE item_instance (guid INTEGER, itemEntry INTEGER, durability INTEGER, enchantments TEXT);
            INSERT INTO character_inventory VALUES (1004,0,0,101), (1004,0,14,102), (1004,0,15,103), (1004,0,2,104), (1004,1,5,105);
            INSERT INTO item_instance VALUES (101,39403,80,'original enchant'),(102,50653,0,''),(103,50730,100,''),(104,39249,90,''),(105,12345,0,'');
        ''')
    def tearDown(self):
        self.db.close()
    def test_unsupported_has_no_table_dependency(self):
        rows = self.db.execute(queries[1]).fetchall()
        self.assertEqual(len(rows), 4)
        self.assertTrue(all(row[4] == 0 for row in rows))
    def test_separate_metadata_hidden_and_owner_check(self):
        self.db.executescript('''
            CREATE TABLE custom_transmogrification (GUID INTEGER PRIMARY KEY, FakeEntry INTEGER, Owner INTEGER);
            INSERT INTO custom_transmogrification VALUES (101,16955,1004),(102,1,1004),(103,22691,1004),(104,16953,9999),(105,16952,1004);
        ''')
        original = self.db.execute(queries[1]).fetchall()
        dressed = self.db.execute(queries[0]).fetchall()
        self.assertEqual([r[:4] for r in original], [r[:4] for r in dressed])
        self.assertEqual({r[0]:r[4] for r in dressed}, {0:16955,2:None,14:1,15:22691})

if __name__ == '__main__':
    unittest.main()
