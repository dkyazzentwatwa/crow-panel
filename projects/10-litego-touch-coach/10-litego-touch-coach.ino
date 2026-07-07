#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
char board[9][9];
char toMove = 'B';
uint16_t moveCount = 0;

String nextWord(String &line) {
  line.trim();
  int space = line.indexOf(' ');
  if (space < 0) {
    String word = line;
    line = "";
    return word;
  }
  String word = line.substring(0, space);
  line = line.substring(space + 1);
  return word;
}

void refreshGo(const String &banner) {
  dashboard.setTile(0, "Board", "9x9", "coach play");
  dashboard.setTile(1, "To Move", String(toMove), "Black starts");
  dashboard.setTile(2, "Moves", String(moveCount), "local game");
  dashboard.setTile(3, "Coach", "liberties", "atari hints");
  dashboard.setTile(4, "Score", "rough", "area estimate");
  dashboard.setBanner(banner);
  dashboard.setFooter("LiteGo v1 is local/offline C++ logic inspired by ai-go");
}

uint8_t libertiesAt(int x, int y) {
  char c = board[y][x];
  if (c == '.') return 0;
  uint8_t libs = 0;
  if (x > 0 && board[y][x - 1] == '.') libs++;
  if (x < 8 && board[y][x + 1] == '.') libs++;
  if (y > 0 && board[y - 1][x] == '.') libs++;
  if (y < 8 && board[y + 1][x] == '.') libs++;
  return libs;
}

void printBoard() {
  for (uint8_t y = 0; y < 9; y++) {
    Serial.print(y);
    Serial.print(F(" "));
    for (uint8_t x = 0; x < 9; x++) Serial.print(board[y][x]);
    Serial.println();
  }
}

void resetBoard() {
  for (uint8_t y = 0; y < 9; y++) {
    for (uint8_t x = 0; x < 9; x++) board[y][x] = '.';
  }
  toMove = 'B';
  moveCount = 0;
}

void cmdStatus(const String &) { printSystemStatus(Serial, "litego-coach", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdPlay(const String &args) {
  String rest = args;
  int x = nextWord(rest).toInt();
  int y = nextWord(rest).toInt();
  if (x < 0 || x > 8 || y < 0 || y > 8 || board[y][x] != '.') {
    Serial.println(F("[go] illegal or occupied"));
    dashboard.setDetail("Illegal Move", "Use play <x> <y>|Coordinates are 0-8|Occupied points are rejected");
    refreshGo("illegal move");
    return;
  }
  board[y][x] = toMove;
  uint8_t libs = libertiesAt(x, y);
  moveCount++;
  Serial.println(String("[go] ") + String(toMove) + " at " + String(x) + "," + String(y) + " libs=" + String(libs));
  eventLog.add(String("Move ") + String(toMove) + " " + String(x) + "," + String(y));
  dashboard.setDetail("Coach", String(toMove) + " played at " + String(x) + "," + String(y) + "|Immediate liberties: " + String(libs) + (libs <= 1 ? "|Atari shape: urgent" : "|Shape is stable enough for now"));
  toMove = (toMove == 'B') ? 'W' : 'B';
  printBoard();
  refreshGo("move played");
}

void cmdCpu(const String &) {
  for (uint8_t y = 0; y < 9; y++) {
    for (uint8_t x = 0; x < 9; x++) {
      if (board[y][x] == '.') {
        String cmd = String(x) + " " + String(y);
        cmdPlay(cmd);
        return;
      }
    }
  }
}

void cmdPass(const String &) {
  toMove = (toMove == 'B') ? 'W' : 'B';
  eventLog.add("Pass");
  dashboard.setDetail("Pass", String("Turn passed|Next to move: ") + String(toMove));
  refreshGo("turn passed");
}

void cmdReset(const String &) {
  resetBoard();
  eventLog.add("Board reset");
  dashboard.setDetail("New Game", "9x9 board reset|Black to move|Use play x y");
  refreshGo("new game");
}

void cmdScore(const String &) {
  uint8_t black = 0;
  uint8_t white = 0;
  for (uint8_t y = 0; y < 9; y++) {
    for (uint8_t x = 0; x < 9; x++) {
      if (board[y][x] == 'B') black++;
      if (board[y][x] == 'W') white++;
    }
  }
  Serial.println(String("[score] black=") + String(black) + " white=" + String(white));
  dashboard.setDetail("Score Estimate", String("Black stones: ") + String(black) + "|White stones: " + String(white) + "|Territory scoring is staged");
  refreshGo("score estimate");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel LiteGo Touch Coach");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("litego");
  resetBoard();
  dashboard.begin("LITEGO", "TOUCH COACH", "LOCAL");
  refreshGo("coach board ready");
  eventLog.add("LiteGo Touch Coach booted");
  router.begin(Serial, "litego");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("play", "play <x> <y>", cmdPlay);
  router.on("cpu", "make simple CPU move", cmdCpu);
  router.on("pass", "pass turn", cmdPass);
  router.on("reset", "reset board", cmdReset);
  router.on("score", "rough score estimate", cmdScore);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
