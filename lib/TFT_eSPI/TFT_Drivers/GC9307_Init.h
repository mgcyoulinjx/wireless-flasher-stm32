
{
  writecommand(ST7789_SLPOUT); // Sleep out
  delay(120);

  writecommand(ST7789_NORON); // Normal display mode on

  //------------------------------display and color format setting--------------------------------//
  writecommand(ST7789_MADCTL);
  // writedata(0x00);
  writedata(TFT_MAD_COLOR_ORDER);

  writecommand(ST7789_COLMOD);
  writedata(0x55);
  delay(10);

  writecommand(0x36); writedata(0x00);
  writecommand(0x3A); writedata(0x05);
  writecommand(0xfe);
  writecommand(0xef);
  writecommand(0x36); writedata(0x48);
  writecommand(0x3a); writedata(0x05);
  writecommand(0x84); writedata(0x40);
  writecommand(0x86); writedata(0x98);
  writecommand(0x89); writedata(0x13);
  writecommand(0x8b); writedata(0x80);
  writecommand(0x8d); writedata(0x33);
  writecommand(0x8e); writedata(0x0f);
  writecommand(0xb6); writedata(0x00); writedata(0x00); writedata(0x24);
  writecommand(0xe8); writedata(0x13); writedata(0x00);
  writecommand(0xEC); writedata(0x33); writedata(0x00); writedata(0xF0);
  writecommand(0xff); writedata(0x62);
  writecommand(0x99); writedata(0x3e);
  writecommand(0x9d); writedata(0x4b);
  writecommand(0x98); writedata(0x3e);
  writecommand(0x9c); writedata(0x4b);
  writecommand(0xc3); writedata(0x2A);
  writecommand(0xc4); writedata(0x14);
  writecommand(0xc9); writedata(0x34);
  writecommand(0xF0); writedata(0x1D); writedata(0x21); writedata(0x0C); writedata(0x0B); writedata(0x06); writedata(0x43);
  writecommand(0xF1); writedata(0x56); writedata(0x78); writedata(0x94); writedata(0x2C); writedata(0x2D); writedata(0xAF);
  writecommand(0xF2); writedata(0x1D); writedata(0x21); writedata(0x0C); writedata(0x0B); writedata(0x06); writedata(0x43);
  writecommand(0xF3); writedata(0x56); writedata(0x78); writedata(0x94); writedata(0x2C); writedata(0x2D); writedata(0xAF);

  writecommand(ST7789_CASET); // Column address set
  writedata(0x00);
  writedata(0x00);
  writedata(0x00);
  writedata(0xE5); // 239

  writecommand(ST7789_RASET); // Row address set
  writedata(0x00);
  writedata(0x00);
  writedata(0x01);
  writedata(0x3F); // 319

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  end_tft_write();
  delay(120);
  begin_tft_write();

  writecommand(ST7789_DISPON); // Display on
  delay(120);

}
