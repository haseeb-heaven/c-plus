#include <stdio.h>
#include <stddef.h>

/* Real-world style: packet header, packed wire header, nested point and
   sampled-readings record. */
struct Header { unsigned char tag; int seq; double stamp; short flags; };
#pragma pack(push, 1)
struct WireHeader { unsigned char tag; int seq; double stamp; };
#pragma pack(pop)
struct Point { int x; int y; };
struct Segment {
  unsigned char id; struct Point a; struct Point b; short weight;
};
struct Samples { unsigned char id; long values[3]; double scale; };
int main(void) {
  printf("header=%zu,%zu,%zu,%zu,%zu\n", sizeof(struct Header),
         offsetof(struct Header, tag), offsetof(struct Header, seq),
         offsetof(struct Header, stamp), offsetof(struct Header, flags));
  printf("wire=%zu,%zu,%zu,%zu\n", sizeof(struct WireHeader),
         offsetof(struct WireHeader, tag), offsetof(struct WireHeader, seq),
         offsetof(struct WireHeader, stamp));
  printf("seg=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
         sizeof(struct Segment), offsetof(struct Segment, id),
         offsetof(struct Segment, a), offsetof(struct Segment, b),
         offsetof(struct Segment, weight), offsetof(struct Segment, a.x),
         offsetof(struct Segment, a.y), offsetof(struct Segment, b.x),
         offsetof(struct Segment, b.y));
  printf("samples=%zu,%zu,%zu,%zu\n", sizeof(struct Samples),
         offsetof(struct Samples, id), offsetof(struct Samples, values),
         offsetof(struct Samples, scale));
  return 0;
}
