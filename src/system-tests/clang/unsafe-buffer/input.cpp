// Exercise the actual compiler's unsafe-buffer fix-it owners without headers.
void local(int index) {
  int *p = new int[10];
  bool zero = &p[0];
  bool other = &p[index];
  bool advance = ++p;
}
int pick(int *p);
int pick(int *p) { return p[1]; }
