/* two dimensional inverse discrete cosine transform */
void Fast_IDCT(block, comp)
  short *block;
  int comp;
{
    idct_c (block);
    //idct_mmx (block, comp);
    //idct_altivec (block);
}
