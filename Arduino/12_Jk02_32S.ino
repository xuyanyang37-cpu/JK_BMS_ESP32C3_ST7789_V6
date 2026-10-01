class Jk02_32S {
public:
  static const uint8_t MAX_CELLS=32;
  static const int DATA_OFFSET=32;
  static const size_t FRAME_LENGTH=300;
  static const char* name(){return "JK02_32S";}
};

// JK02_32S 模型参数集中放在这里，便于后续根据实机帧继续修正。