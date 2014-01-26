#include "dropblox_ai.h"

using namespace json;
using namespace std;

//----------------------------------
// Block implementation starts here!
//----------------------------------

Block::Block(Object& raw_block) {
  center.i = (int)(Number&)raw_block["center"]["i"];
  center.j = (int)(Number&)raw_block["center"]["j"];
  size = 0;

  Array& raw_offsets = raw_block["offsets"];
  for (Array::const_iterator it = raw_offsets.Begin(); it < raw_offsets.End(); it++) {
    size += 1;
  }
  for (int i = 0; i < size; i++) {
    offsets[i].i = (Number&)raw_offsets[i]["i"];
    offsets[i].j = (Number&)raw_offsets[i]["j"];
  }

  translation.i = 0;
  translation.j = 0;
  rotation = 0;
}

void Block::left() {
  translation.j -= 1;
}

void Block::right() {
  translation.j += 1;
}

void Block::up() {
  translation.i -= 1;
}

void Block::down() {
  translation.i += 1;
}

void Block::rotate() {
  rotation += 1;
}

void Block::unrotate() {
  rotation -= 1;
}

// The checked_* methods below perform an operation on the block
// only if it's a legal move on the passed in board.  They
// return true if the move succeeded.
//
// The block is still assumed to start in a legal position.
bool Block::checked_left(const Board& board) {
  left();
  if (board.check(*this)) {
    return true;
  }
  right();
  return false;
}

bool Block::checked_right(const Board& board) {
  right();
  if (board.check(*this)) {
    return true;
  }
  left();
  return false;
}

bool Block::checked_up(const Board& board) {
  up();
  if (board.check(*this)) {
    return true;
  }
  down();
  return false;
}

bool Block::checked_down(const Board& board) {
  down();
  if (board.check(*this)) {
    return true;
  }
  up();
  return false;
}

bool Block::checked_rotate(const Board& board) {
  rotate();
  if (board.check(*this)) {
    return true;
  }
  unrotate();
  return false;
}

bool Block::check_left(const Board& board) {
  left();
  if (board.check(*this)) {
    right();
    return true;
  }
  right();
  return false;
}

bool Block::check_right(const Board& board) {
  right();
  if (board.check(*this)) {
    left();
    return true;
  }
  left();
  return false;
}

bool Block::check_up(const Board& board) {
  up();
  if (board.check(*this)) {
    down();
    return true;
  }
  down();
  return false;
}

bool Block::check_down(const Board& board) {
  down();
  if (board.check(*this)) {
    up();
    return true;
  }
  up();
  return false;
}

bool Block::check_rotate(const Board& board) {
  rotate();
  if (board.check(*this)) {
    unrotate();
    return true;
  }
  unrotate();
  return false;
}

void Block::set_position(const position& pos)
{
  translation.i = pos.i-I_START;
  translation.j = pos.j-J_START;
  rotation = pos.rotation;
}

void Block::do_command(const string& command) {
  if (command == "left") {
    left();
  } else if (command == "right") {
    right();
  } else if (command == "up") {
    up();
  } else if (command == "down") {
    down();
  } else if (command == "rotate") {
    rotate();
  } else {
    throw Exception("Invalid command " + command);
  }
}

void Block::do_commands(const vector<string>& commands) {
  for (int i = 0; i < commands.size(); i++) {
    do_command(commands[i]);
  }
}

void Block::reset_position() {
  translation.i = 0;
  translation.j = 0;
  rotation = 0;
}

//----------------------------------
// Board implementation starts here!
//----------------------------------

Board::Board() {
  rows = ROWS;
  cols = COLS;
}

Board::Board(Object& state) {
  rows = ROWS;
  cols = COLS;

  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      bitmap[i][j] = ((int)(Number&)state["bitmap"][i][j] ? 1 : 0);
    }
  }

  // Note that these blocks are NEVER destructed! This is because calling
  // place() on a board will create new boards which share these objects.
  //
  // There's a memory leak here, but it's okay: blocks are only constructed
  // when you construct a board from a JSON Object, which should only happen
  // for the very first board. The total memory leaked will only be ~10 kb.
  block = new Block(state["block"]);
  for (int i = 0; i < PREVIEW_SIZE; i++) {
    preview.push_back(new Block(state["preview"][i]));
  }
}

// Returns true if the `query` block is in valid position - that is, if all of
// its squares are in bounds and are currently unoccupied.
bool Board::check(const Block& query) const {
  Point point;
  for (int i = 0; i < query.size; i++) {
    point.i = query.center.i + query.translation.i;
    point.j = query.center.j + query.translation.j;
    if (query.rotation % 2) {
      point.i += (2 - query.rotation)*query.offsets[i].j;
      point.j +=  -(2 - query.rotation)*query.offsets[i].i;
    } else {
      point.i += (1 - query.rotation)*query.offsets[i].i;
      point.j += (1 - query.rotation)*query.offsets[i].j;
    }
    if (point.i < 0 || point.i >= ROWS ||
        point.j < 0 || point.j >= COLS || bitmap[point.i][point.j]) {
      return false;
    }
  }
  return true;
}

// Resets the block's position, moves it according to the given commands, then
// drops it onto the board. Returns a pointer to the new board state object.
//
// Throws an exception if the block is ever in an invalid position.
Board* Board::do_commands(const vector<string>& commands) {
  block->reset_position();
  if (!check(*block)) {
    throw Exception("Block started in an invalid position");
  }
  for (int i = 0; i < commands.size(); i++) {
    if (commands[i] == "drop") {
      return place();
    } else {
      block->do_command(commands[i]);
      if (!check(*block)) {
        throw Exception("Block reached in an invalid position");
      }
    }
  }
  // If we've gotten here, there was no "drop" command. Drop anyway.
  return place();
}

// Drops the block from whatever position it is currently at. Returns a
// pointer to the new board state object, with the next block drawn from the
// preview list.
//
// Assumes the block starts out in valid position.
// This method translates the current block downwards.
//
// If there are no blocks left in the preview list, this method will fail badly!
// This is okay because we don't expect to look ahead that far.
Board* Board::place() {
  Board* new_board = new Board();

  while (check(*block)) {
    block->down();
  }
  block->up();

  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      new_board->bitmap[i][j] = bitmap[i][j];
    }
  }

  Point point;
  for (int i = 0; i < block->size; i++) {
    point.i = block->center.i + block->translation.i;
    point.j = block->center.j + block->translation.j;
    if (block->rotation % 2) {
      point.i += (2 - block->rotation)*block->offsets[i].j;
      point.j +=  -(2 - block->rotation)*block->offsets[i].i;
    } else {
      point.i += (1 - block->rotation)*block->offsets[i].i;
      point.j += (1 - block->rotation)*block->offsets[i].j;
    }
    new_board->bitmap[point.i][point.j] = 1;
  }
  Board::remove_rows(&(new_board->bitmap));

  new_board->block = preview[0];
  for (int i = 1; i < preview.size(); i++) {
    new_board->preview.push_back(preview[i]);
  }

  return new_board;
}

// A static method that takes in a new_bitmap and removes any full rows from it.
// Mutates the new_bitmap in place.
void Board::remove_rows(Bitmap* new_bitmap) {
  int rows_removed = 0;
  for (int i = ROWS - 1; i >= 0; i--) {
    bool full = true;
    for (int j = 0; j < COLS; j++) {
      if (!(*new_bitmap)[i][j]) {
        full = false;
        break;
      }
    }
    if (full) {
      rows_removed += 1;
    } else if (rows_removed) {
      for (int j = 0; j < COLS; j++) {
        (*new_bitmap)[i + rows_removed][j] = (*new_bitmap)[i][j];
      }
    }
  }
  for (int i = 0; i < rows_removed; i++) {
    for (int j = 0; j < COLS; j++) {
      (*new_bitmap)[i][j] = 0;
    }
  }
}

bool equals_start(const position& pos)
{
  return (pos.i == I_START && pos.j == J_START && pos.rotation == 0);
}

bool is_reachable(const int array[33][23], const position& pos)
{
  return (array[pos.i][pos.j] & (1<<pos.rotation)) != 0;
}

void set_reachable(int array[33][23], const position& pos)
{
  array[pos.i][pos.j] = array[pos.i][pos.j] | (1 << pos.rotation);
}

void flood_fill(Board* board, int array[33][23], MoveType move_history[33][23][4])
{
  memset(array, 0, sizeof(array));
  memset(move_history, 0, sizeof(move_history));

  position current;
  current.i = I_START;
  current.j = J_START;
  current.rotation = 0;

  queue<position> q;
  q.push(current);

  do
  {
    current = q.front();
    board->block->set_position(current);
    if(!is_reachable(array, current) && board->check(*board->block))
    {
      set_reachable(array, current);
      MoveType hist;

      if(current.j != 0)
      {
        position newp = current;
        newp.j--;
        if(!is_reachable(array, newp))
        {
          hist = mleft;
          move_history[newp.i][newp.j][newp.rotation] = hist;
          q.push(newp);
        }
      }
      if(current.j != 22)
      {
        position newp = current;
        newp.j++;
        if(!is_reachable(array, newp))
        {
          hist = mright;
          move_history[newp.i][newp.j][newp.rotation] = hist;
          q.push(newp);
        }
      }
      if(current.i != 0)
      {
        position newp = current;
        newp.i--;
        if(!is_reachable(array, newp))
        {
          hist = mup;
          move_history[newp.i][newp.j][newp.rotation] = hist;
          q.push(newp);
        }
      }
      if(current.i != 32)
      {
        position newp = current;
        newp.i++;
        if(!is_reachable(array, newp))
        {
          hist = mdown;
          move_history[newp.i][newp.j][newp.rotation] = hist;
          q.push(newp);
        }
      }

      position newp = current;
      newp.rotation = (newp.rotation+1)%4;
      if(!is_reachable(array, newp))
      {
        hist = mrotate;
        move_history[newp.i][newp.j][newp.rotation] = hist;
        q.push(newp);
      }
    }
    q.pop();
  } while(!q.empty());
  board->block->reset_position();
}

//START = (9,11)

vector<string>* find_path_to(Board* board, MoveType move_history[33][23][4], const position& find)
{
  stack<MoveType> moves;
  vector<string>* move_strings = new vector<string>();

  position current = find;
  while(!equals_start(current))
  {
    moves.push(move_history[current.i][current.j][current.rotation]);
    std::cout << current.i << " " << current.j << " " << current.rotation << endl;
    switch(move_history[current.i][current.j][current.rotation])
    {
      case mleft:
        current.j++;
        break;
      case mright:
        current.j--;
        break;
      case mup:
        current.i++;
        break;
      case mdown:
        current.i--;
        break;
      case mrotate:
        current.rotation = (current.rotation+3)%4;
        break;
      default:
        cout << "Something went wrong" << endl;
        assert(false);
    }
  }

  while(!moves.empty())
  {
    MoveType last_move = moves.top();
    switch(last_move)
    {
      case mleft:
        move_strings->push_back("left");
        break;
      case mright:
        move_strings->push_back("right");
        break;
      case mup:
        move_strings->push_back("up");
        break;
      case mdown:
        move_strings->push_back("down");
        break;
      case mrotate:
        move_strings->push_back("rotate");
        break;
    }
    moves.pop();
  }

  return move_strings;
}

int main(int argc, char** argv) {
  // Construct a JSON Object with the given game state.
  istringstream raw_state(argv[1]);
  Object state;
  Reader::Read(state, raw_state);

  // Construct a board from this Object.
  Board board(state);
  /*
  MoveType move_history[33][23][4];
  int array[33][23];
  flood_fill(&board, array, move_history);
  position test;
  test.i = 12; test.j = 22; test.rotation = 2;
  std::vector<string>* moves_s = find_path_to(&board, move_history, test);
  
  for(int i = 0; i < moves_s->size(); i++)
  {
    cout << "move " << moves_s->at(i) << endl;
  }
  return 0;
  */
  // Make some moves!
  vector<string> moves;
  while (board.check(*board.block)) {
    board.block->left();
    moves.push_back("left");
  }
  // Ignore the last move, because it moved the block into invalid
  // position. Make all the rest.
  for (int i = 0; i < moves.size() - 1; i++) {
    cout << moves[i] << endl;
  }
}
