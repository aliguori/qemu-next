/*
 *  Functions used by host & client sides
 * 
 *  Copyright (c) 2007 Even Rouault
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#ifndef _OPENGL_UTILS
#define _OPENGL_UTILS

typedef struct
{
  unsigned int* values;
  int nbValues;
} RangeAllocator;

/*
static void print_range(RangeAllocator* range)
{
  int i;
  printf("%s", "table : ");
  for(i=0;i<range->nbValues;i++)
  {
    printf("%d ", range->values[i]);
  }
  printf("\n");
}
*/

static void alloc_value(RangeAllocator* range, unsigned int value)
{
  if (value == 0) return;
  if (range->nbValues >= 1)
  {
    int lower = 0;
    int upper = range->nbValues-1;
    while(1)
    {
      int mid = (lower + upper) / 2;
      if (range->values[mid] > value)
        upper = mid;
      else if (range->values[mid] < value)
        lower = mid;
      else
        break;
      if (upper - lower <= 1)
      {
        if (value < range->values[lower])
        {
          range->values = realloc(range->values, (range->nbValues+1) * sizeof(int));
          memmove(&range->values[lower+1], &range->values[lower], (range->nbValues - lower) * sizeof(int));
          range->values[lower] = value;
          range->nbValues++;
        }
        else if (value == range->values[lower])
        {
        }
        else if (value < range->values[upper])
        {
          range->values = realloc(range->values, (range->nbValues+1) * sizeof(int));
          memmove(&range->values[upper+1], &range->values[upper], (range->nbValues - upper) * sizeof(int));
          range->values[upper] = value;
          range->nbValues++;
        }
        else if (value == range->values[upper])
        {
        }
        else
        {
          upper++;
          
          range->values = realloc(range->values, (range->nbValues+1) * sizeof(int));
          memmove(&range->values[upper+1], &range->values[upper], (range->nbValues - upper) * sizeof(int));
          range->values[upper] = value;
          range->nbValues++;
        }
        break;
      }
    }
  }
  else
  {
    range->values = malloc(sizeof(int));
    range->values[0] = value;
    range->nbValues = 1;
  }
}

/* return first value */
static unsigned int alloc_range(RangeAllocator* range, int n, unsigned int* values)
{
  int i, j;
  if (range->nbValues == 0)
  {
    range->nbValues = n;
    range->values = malloc(n * sizeof(int));
    for(i=0;i<n;i++)
    {
      range->values[i] = i+1;
      if (values)
        values[i] = range->values[i];
    }
    return 1;
  }
  else
  {
    int lastValue = 1;
    for(i=0;i<range->nbValues;i++)
    {
      if ((int)range->values[i] - (int)lastValue - 1 >= n)
      {
        range->values = realloc(range->values, (range->nbValues+n) * sizeof(int));
        memmove(&range->values[i+n], &range->values[i], (range->nbValues - i) * sizeof(int));
        for(j=0;j<n;j++)
        {
          range->values[i+j] = lastValue + 1 + j;
          if (values)
            values[j] = range->values[i+j];
        }
        range->nbValues += n;
        break;
      }
      else
        lastValue = range->values[i];
    }
    if (i == range->nbValues)
    {
      range->values = realloc(range->values, (range->nbValues+n) * sizeof(int));
      for(j=0;j<n;j++)
      {
        range->values[i+j] = lastValue + 1 + j;
        if (values)
          values[j] = range->values[i+j];
      }
      range->nbValues += n;
    }
    return lastValue + 1;
  }
}

static void delete_value(RangeAllocator* range, unsigned int value)
{
  if (value == 0)
    return;
  if (range->nbValues >= 1)
  {
    int lower = 0;
    int upper = range->nbValues-1;
    while(1)
    {
      int mid = (lower + upper) / 2;
      if (range->values[mid] > value)
        upper = mid;
      else if (range->values[mid] < value)
        lower = mid;
      else
      {
        lower = upper = mid;
      }
      if (upper - lower <= 1)
      {
        if (value == range->values[lower])
        {
          memmove(&range->values[lower], &range->values[lower+1], (range->nbValues - lower-1) * sizeof(int));
          range->nbValues--;
        }
        else if (value == range->values[upper])
        {
          memmove(&range->values[upper], &range->values[upper+1], (range->nbValues - upper-1) * sizeof(int));
          range->nbValues--;
        }
        break;
      }
    }
  }
}

static void delete_range(RangeAllocator* range, int n, const unsigned int* values)
{
  int i;
  for(i=0;i<n;i++)
  {
    delete_value(range, values[i]);
  }
}

static void delete_consecutive_values(RangeAllocator* range, unsigned int first, int n)
{
  int i;
  for(i=0;i<n;i++)
  {
    delete_value(range, first + i);
  }
}

#endif
