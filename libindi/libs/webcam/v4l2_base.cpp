/*
    Copyright (C) 2005 by Jasem Mutlaq <mutlaqja@ikarustech.com>

    Based on V4L 2 Example
    http://v4l2spec.bytesex.org/spec-single/v4l2.html#CAPTURE-EXAMPLE

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/ 

#include <iostream>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <asm/types.h>          /* for videodev2.h */

#include "ccvt.h"
#include "v4l2_base.h"
#include "eventloop.h"
#include "indidevapi.h"
#include "lilxml.h"

/* PWC framerate support*/
#include "pwc-ioctl.h"

#define ERRMSGSIZ	1024

#define CLEAR(x) memset (&(x), 0, sizeof (x))

using namespace std;

/*char *entityXML(char *src) {
  char *s = src;
  while (*s) {
    if ((*s == '&') || (*s == '<') || (*s == '>') || (*s == '\'') || (*s == '"'))
      *s = '_';
    s += 1;
  }
  return src;
}*/

V4L2_Base::V4L2_Base()
{

   frameRate.numerator=1;
   frameRate.denominator=25;

   selectCallBackID = -1;
   //dropFrameCount = 1;
   //dropFrame = 0;
  
   xmax = xmin = 160;
   ymax = ymin = 120;

   io		= IO_METHOD_MMAP;
   fd           = -1;
   buffers      = NULL;
   n_buffers    = 0;


   callback     = NULL;

   cancrop=true;
   cansetrate=true;
   streamedonce=false;   
   v4l2_decode=new V4L2_Decode();
   decoder=v4l2_decode->getDefaultDecoder();
   decoder->init();
   dodecode=true;
   bpp=8; 
   has_ext_pix_format=false;
   const std::vector<unsigned int> &vsuppformats=decoder->getsupportedformats();
   IDLog("Using default decoder '%s'\n  Supported V4L2 formats are:\n  ", decoder->getName());
   for (std::vector<unsigned int>::const_iterator it=vsuppformats.begin(); it!=vsuppformats.end(); ++it)
     IDLog("%c%c%c%c ", (*it >> 0), (*it >> 8), (*it >>16), (*it >> 24));
   IDLog("\n");
   //DEBUGF(INDI::Logger::DBG_SESSION,"Default decoder: %s", decoder->getName());
}

V4L2_Base::~V4L2_Base()
{

}

int V4L2_Base::xioctl(int fd, int request, void *arg)
{
        int r;

        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);

        return r;
}

int V4L2_Base::errno_exit(const char *s, char *errmsg)
{
        fprintf (stderr, "%s error %d, %s\n",
                 s, errno, strerror (errno));
     
        snprintf(errmsg, ERRMSGSIZ, "%s error %d, %s\n", s, errno, strerror (errno));
	
	if (streamactive) 
	  stop_capturing(errmsg);
	
        return -1;
} 

void V4L2_Base::doDecode(bool d) {
  dodecode=d;
}

void V4L2_Base::doRecord(bool d) {
  dorecord=d;
}


void V4L2_Base::setRecorder(V4L2_Recorder *r) {
  recorder=r;
}

int V4L2_Base::connectCam(const char * devpath, char *errmsg , int pixelFormat , int width , int height)
{
   selectCallBackID = -1;
   cancrop=true;
   cansetrate=true;
   streamedonce=false;
   frameRate.numerator=1;
   frameRate.denominator=25;

    if (open_device (devpath, errmsg) < 0)
      return -1;

    path=devpath;

    if (check_device(errmsg) < 0)
      return -1;

   cerr << "V4L2 Check: All successful, returning\n";
   return fd;
}

void V4L2_Base::disconnectCam(bool stopcapture)
{
   char errmsg[ERRMSGSIZ];

   
   if (selectCallBackID != -1)
     rmCallback(selectCallBackID);
     
   if (stopcapture) 
     stop_capturing (errmsg);

   //uninit_device (errmsg);

   close_device ();
     
   fprintf(stderr, "Disconnect cam\n");
}

bool V4L2_Base::isLXmodCapable()
{
  if (!(strcmp((const char *)cap.driver, "pwc")))
    return true;
  else return false;
}



int V4L2_Base::read_frame(char *errmsg) {
  
  unsigned int i;
  //cerr << "in read Frame" << endl;
  
  switch (io) {
  case IO_METHOD_READ:
    cerr << "in read Frame method read" << endl;
    if (-1 == read (fd, buffers[0].start, buffers[0].length)) {
      switch (errno) {
      case EAGAIN:
	return 0;
      case EIO:
	/* Could ignore EIO, see spec. */
	/* fall through */
      default:
	return errno_exit ("read", errmsg);
      }
    }
    //process_image (buffers[0].start);
    break;
  
  case IO_METHOD_MMAP:
    //cerr << "in read Frame method mmap" << endl;
    CLEAR (buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
      switch (errno) {
      case EAGAIN:
	//cerr << "in read Frame method DQBUF EAGAIN" << endl;
	return 0;
      case EIO:
	/* Could ignore EIO, see spec. */
	/* fall through */
      default:
	return errno_exit ("ReadFrame IO_METHOD_MMAP: VIDIOC_DQBUF", errmsg);
      }
    }

    assert (buf.index < n_buffers);
    //IDLog("drop %c %d on %d\n", (dropFrameEnabled?'Y':'N'),dropFrame, dropFrameCount);
    /*if (dropFrame > 0)
      {
        dropFrame -= 1;
	if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
	  return errno_exit ("ReadFrame IO_METHOD_MMAP: VIDIOC_QBUF", errmsg);
        return 0;
      }
    else
        dropFrame = dropFrameCount;*/


    /*
      switch (buf.flags &  V4L2_BUF_FLAG_TIMESTAMP_MASK) {
    case V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN: IDLog("Timestamp Unknown\n"); break;
    case V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC : IDLog("Timestamp Monotonic\n"); break;
    case V4L2_BUF_FLAG_TIMESTAMP_COPY : IDLog("Timestamp Copy\n"); break;
    default: break;
    }
    switch (buf.flags &  V4L2_BUF_FLAG_TSTAMP_SRC_MASK) {
    case V4L2_BUF_FLAG_TSTAMP_SRC_EOF: IDLog("Timestamp at End of Frame\n"); break;
    case V4L2_BUF_FLAG_TSTAMP_SRC_SOE : IDLog("Timestamp at Start of Exposure\n"); break;
    default: break;
    }
    */
    //IDLog("v4l2_base: dequeuing buffer %d, bytesused = %d, flags = 0x%X, field = %d, sequence = %d\n", buf.index, buf.bytesused, buf.flags, buf.field, buf.sequence);
    //IDLog("v4l2_base: dequeuing buffer %d for fd=%d, cropset %c\n", buf.index, fd, (cropset?'Y':'N'));
    //IDLog("V4L2_base read_frame: calling decoder (@ %x) %c\n", decoder, (dodecode?'Y':'N'));
    if (dodecode) decoder->decode((unsigned char *)(buffers[buf.index].start), &buf);
    //IDLog("V4L2_base read_frame: calling recorder(@ %x) %c\n", recorder, (dorecord?'Y':'N'));
    if (dorecord) recorder->writeFrame((unsigned char *)(buffers[buf.index].start));
    
    //IDLog("lxstate is %d, dropFrame %c\n", lxstate, (dropFrame?'Y':'N'));



    if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
      return errno_exit ("ReadFrame IO_METHOD_MMAP: VIDIOC_QBUF", errmsg);



    if( lxstate == LX_ACTIVE ) {

      /* Call provided callback function if any */
      //if (callback && !dorecord)
      if (callback)
	(*callback)(uptr);
    }
		
    if( lxstate == LX_TRIGGERED )
      lxstate = LX_ACTIVE;
    break;
    
  case IO_METHOD_USERPTR:
    cerr << "in read Frame method userptr" << endl;
    CLEAR (buf);
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;
    
    if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
      switch (errno) {
      case EAGAIN:
	return 0;
      case EIO:
	/* Could ignore EIO, see spec. */
	/* fall through */
      default:
	errno_exit ("VIDIOC_DQBUF", errmsg);
      }
    }
    
    for (i = 0; i < n_buffers; ++i)
      if (buf.m.userptr == (unsigned long) buffers[i].start
	  && buf.length == buffers[i].length)
	break;
    
    assert (i < n_buffers);
    
    //process_image ((void *) buf.m.userptr);
    
    if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
      errno_exit ("ReadFrame IO_METHOD_USERPTR: VIDIOC_QBUF", errmsg);
    
    break;
  }
  
  return 0;
}

int V4L2_Base::stop_capturing(char *errmsg) {
  enum v4l2_buf_type type;
  unsigned int i;
  switch (io) {
  case IO_METHOD_READ:
    /* Nothing to do. */
    break;
    
  case IO_METHOD_MMAP:
    /* Kernel 3.11 problem with streamoff: vb2_is_busy(queue) remains true so we can not change anything without diconnecting */
    /* It seems that device should be closed/reopened to change any capture format settings. From V4L2 API Spec. (Data Negotiation) */
    /* Switching the logical stream or returning into "panel mode" is possible by closing and reopening the device. */
    /* Drivers may support a switch using VIDIOC_S_FMT. */
  case IO_METHOD_USERPTR:
    // N.B. I used this as a hack to solve a problem with capturing a frame
    // long time ago. I recently tried taking this hack off, and it worked fine!
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    IERmCallback(selectCallBackID);
    selectCallBackID = -1;
    streamactive = false;
    if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
      return errno_exit ("VIDIOC_STREAMOFF", errmsg);
    break;
  }
  //uninit_device(errmsg);
  

  return 0;
}

int V4L2_Base::start_capturing(char * errmsg) {
  unsigned int i;
  enum v4l2_buf_type type;
  
  if (!streamedonce) init_device(errmsg);
  
  switch (io) {
  case IO_METHOD_READ:
    /* Nothing to do. */
    break;
    
  case IO_METHOD_MMAP:
    for (i = 0; i < n_buffers; ++i) {
      struct v4l2_buffer buf;
      
      CLEAR (buf);
      
      buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory      = V4L2_MEMORY_MMAP;
      buf.index       = i;
      //IDLog("v4l2_start_capturing: enqueuing buffer %d for fd=%d\n", buf.index, fd);
      /*if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
	return errno_exit ("StartCapturing IO_METHOD_MMAP: VIDIOC_QBUF", errmsg);*/
      xioctl (fd, VIDIOC_QBUF, &buf);
      
    }
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
      return errno_exit ("VIDIOC_STREAMON", errmsg);
    
    selectCallBackID = IEAddCallback(fd, newFrame, this);
    streamactive = true;
    
    break;

  case IO_METHOD_USERPTR:
    for (i = 0; i < n_buffers; ++i) {
      struct v4l2_buffer buf;
      
      CLEAR (buf);
      
      buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory      = V4L2_MEMORY_USERPTR;
      buf.m.userptr	= (unsigned long) buffers[i].start;
      buf.length      = buffers[i].length;
      
      if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
	return errno_exit ("StartCapturing IO_METHOD_USERPTR: VIDIOC_QBUF", errmsg);
    }
     
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
      return errno_exit ("VIDIOC_STREAMON", errmsg);
    
    break;
  }
  //if (dropFrameEnabled)
  //dropFrame = dropFrameCount;
  streamedonce=true;
  return 0;
}

void V4L2_Base::newFrame(int /*fd*/, void *p) {
  char errmsg[ERRMSGSIZ];
  
  ( (V4L2_Base *) (p))->read_frame(errmsg);
  
}

int V4L2_Base::uninit_device(char *errmsg) {

  switch (io) {
  case IO_METHOD_READ:
    free (buffers[0].start);
    break;
    
  case IO_METHOD_MMAP:
    for (unsigned int i = 0; i < n_buffers; ++i)
      if (-1 == munmap (buffers[i].start, buffers[i].length))
	return errno_exit ("munmap", errmsg);
    break;
    
  case IO_METHOD_USERPTR:
    for (unsigned int i = 0; i < n_buffers; ++i)
      free (buffers[i].start);
    break;
  }
  
  free (buffers);
  
  return 0;
}

void V4L2_Base::init_read(unsigned int buffer_size) {
  buffers = (buffer *) calloc (1, sizeof (*buffers));
  
  if (!buffers) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }
  
  buffers[0].length = buffer_size;
  buffers[0].start = malloc (buffer_size);
  
  if (!buffers[0].start) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }
}

int V4L2_Base::init_mmap(char *errmsg) {
  struct v4l2_requestbuffers req;
  
  CLEAR (req);
  
  req.count               = 4;
  //req.count               = 1;
  req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory              = V4L2_MEMORY_MMAP;
  
  if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf (stderr, "%s does not support "
	       "memory mapping\n", dev_name);
      snprintf(errmsg, ERRMSGSIZ, "%s does not support "
	       "memory mapping\n", dev_name);
      return -1;
    } else {
      return errno_exit ("VIDIOC_REQBUFS", errmsg);
    }
  }
  
  if (req.count < 2) {
    fprintf (stderr, "Insufficient buffer memory on %s\n",
	     dev_name);
    snprintf(errmsg, ERRMSGSIZ, "Insufficient buffer memory on %s\n",
	     dev_name);
    return -1;
  }
  
  buffers = (buffer *) calloc (req.count, sizeof (*buffers));
  
  if (!buffers) {
    fprintf (stderr, "buffers. Out of memory\n");
    strncpy(errmsg, "buffers. Out of memory\n", ERRMSGSIZ);
    return -1;
  }
  
  for (n_buffers = 0; n_buffers < req.count; n_buffers++) {
    struct v4l2_buffer buf;
    
    CLEAR (buf);
    
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = n_buffers;
    
    if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
      return errno_exit ("VIDIOC_QUERYBUF", errmsg);
    
    buffers[n_buffers].length = buf.length;
    buffers[n_buffers].start =
      mmap (NULL /* start anywhere */,
	    buf.length,
	    PROT_READ | PROT_WRITE /* required */,
	    MAP_SHARED /* recommended */,
	    fd, buf.m.offset);
    
    if (MAP_FAILED == buffers[n_buffers].start)
      return errno_exit ("mmap", errmsg);
  }
  
  return 0;
}

void V4L2_Base::init_userp(unsigned int buffer_size) {
  struct v4l2_requestbuffers req;
  char errmsg[ERRMSGSIZ];
  
  CLEAR (req);
  
  req.count               = 4;
  req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory              = V4L2_MEMORY_USERPTR;
  
  if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf (stderr, "%s does not support "
	       "user pointer i/o\n", dev_name);
      exit (EXIT_FAILURE);
    } else {
      errno_exit ("VIDIOC_REQBUFS", errmsg);
    }
  }

  buffers = (buffer *) calloc (4, sizeof (*buffers));

  if (!buffers) {
    fprintf (stderr, "Out of memory\n");
    exit (EXIT_FAILURE);
  }
  
  for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
    buffers[n_buffers].length = buffer_size;
    buffers[n_buffers].start = malloc (buffer_size);
    
    if (!buffers[n_buffers].start) {
      fprintf (stderr, "Out of memory\n");
      exit (EXIT_FAILURE);
    }
  }
}

int V4L2_Base::check_device(char *errmsg) {
  unsigned int min;
  struct v4l2_input input_avail;
  ISwitch *inputs=NULL;
  
  if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf (stderr, "%s is no V4L2 device\n",
	       dev_name);
      snprintf(errmsg, ERRMSGSIZ, "%s is no V4L2 device\n", dev_name);
      return -1;
    } else {
      return errno_exit ("VIDIOC_QUERYCAP", errmsg);
    }
  }
  
  IDLog("Driver %s (version %u.%u.%u)\n", cap.driver, (cap.version >> 16) & 0xFF,
	(cap.version >> 8) & 0xFF, (cap.version & 0xFF));   
  IDLog("  card; \t%s\n", cap.card);
  IDLog("  bus; \t%s\n", cap.bus_info);	
  
  setframerate=&V4L2_Base::stdsetframerate;
  getframerate=&V4L2_Base::stdgetframerate;

  if (!(strcmp((const char *)cap.driver, "pwc"))) {
    unsigned int qual=3;
    //pwc driver soes not allow to get current fps with VIDIOCPWC
    //frameRate.numerator=1; // using default module load fps
    //frameRate.denominator=10;
    //if (ioctl(fd, VIDIOCPWCSLED, &qual)) {
    //  IDLog("ioctl: can't set pwc video quality to High (uncompressed).\n");
    //}
    //else
    //  IDLog("  Setting pwc video quality to High (uncompressed)\n");
    //setframerate=&V4L2_Base::pwcsetframerate;
  }

  IDLog("Driver capabilities:\n");
  if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) 
    IDLog("  V4L2_CAP_VIDEO_CAPTURE\n");
  if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) 
    IDLog("  V4L2_CAP_VIDEO_OUTPUT\n");
  if (cap.capabilities & V4L2_CAP_VIDEO_OVERLAY) 
    IDLog("  V4L2_CAP_VIDEO_OVERLAY\n");
  if (cap.capabilities & V4L2_CAP_VBI_CAPTURE) 
    IDLog("  V4L2_CAP_VBI_CAPTURE\n");
  if (cap.capabilities & V4L2_CAP_VBI_OUTPUT) 
    IDLog("  V4L2_CAP_VBI_OUTPUT\n");
  if (cap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) 
    IDLog("  V4L2_CAP_SLICED_VBI_CAPTURE\n");
  if (cap.capabilities & V4L2_CAP_SLICED_VBI_OUTPUT) 
    IDLog("  V4L2_CAP_SLICED_VBI_OUTPUT\n");
  if (cap.capabilities & V4L2_CAP_RDS_CAPTURE) 
    IDLog("  V4L2_CAP_RDS_CAPTURE\n");
  if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) 
    IDLog("  V4L2_CAP_VIDEO_OUTPUT_OVERLAY\n");
  if (cap.capabilities & V4L2_CAP_TUNER) 
    IDLog("  V4L2_CAP_TUNER\n");
  if (cap.capabilities & V4L2_CAP_AUDIO) 
    IDLog("  V4L2_CAP_AUDIO\n");
  if (cap.capabilities & V4L2_CAP_RADIO) 
    IDLog("  V4L2_CAP_RADIO\n");
  if (cap.capabilities & V4L2_CAP_READWRITE) 
    IDLog("  V4L2_CAP_READWRITE\n");
  if (cap.capabilities & V4L2_CAP_ASYNCIO) 
    IDLog("  V4L2_CAP_ASYNCIO\n");
  if (cap.capabilities & V4L2_CAP_STREAMING) 
    IDLog("  V4L2_CAP_STREAMING\n");
  /*if (cap.capabilities & V4L2_CAP_EXT_PIX_FORMAT) {
    has_ext_pix_format=true;
    IDLog("  V4L2_CAP_EXT_PIX_FORMAT\n");
  }*/
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf (stderr, "%s is no video capture device\n",
	     dev_name);
    snprintf(errmsg, ERRMSGSIZ, "%s is no video capture device\n", dev_name);
    return -1;
  }
  
  switch (io) {
  case IO_METHOD_READ:
    if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
      fprintf (stderr, "%s does not support read i/o\n",
	       dev_name);
      snprintf(errmsg, ERRMSGSIZ, "%s does not support read i/o\n",
	       dev_name);
      return -1;
    }
    break;
    
  case IO_METHOD_MMAP:
  case IO_METHOD_USERPTR:
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
      fprintf (stderr, "%s does not support streaming i/o\n",
	       dev_name);
      snprintf(errmsg, ERRMSGSIZ, "%s does not support streaming i/o\n",
	       dev_name);
      return -1;
    }
    
    break;
  }
  
  /* Select video input, video standard and tune here. */

  IDLog("Available Inputs:\n");
  for (input_avail.index=0; ioctl(fd, VIDIOC_ENUMINPUT, &input_avail) != -1; input_avail.index ++) {
    IDLog("\t%d. %s (type %s)\n", input_avail.index, input_avail.name, 
	  (input_avail.type==V4L2_INPUT_TYPE_TUNER?"Tuner/RF Demodulator":"Composite/S-Video"));
  }
  if (errno != EINVAL) IDLog("\tProblem enumerating inputs");
  if (-1 == ioctl (fd, VIDIOC_G_INPUT, &input.index)) {
    perror ("VIDIOC_G_INPUT");
    exit (EXIT_FAILURE);
  }
  IDLog("Current Video input: %d\n", input.index);
  
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  cancrop=true;
  if (-1 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
    perror("VIDIOC_CROPCAP");
    crop.c.top=-1;
    cancrop=false;
    /* Errors ignored. */
  }
  if (cancrop) {
    IDLog("Crop capabilities: bounds = (top=%d, left=%d, width=%d, height=%d)\n", cropcap.bounds.top,
	  cropcap.bounds.left, cropcap.bounds.width, cropcap.bounds.height);
    IDLog("Crop capabilities: defrect = (top=%d, left=%d, width=%d, height=%d)\n", cropcap.defrect.top,
	  cropcap.defrect.left, cropcap.defrect.width, cropcap.defrect.height);
    IDLog("Crop capabilities: pixelaspect = %d / %d\n", cropcap.pixelaspect.numerator, 
	  cropcap.pixelaspect.denominator);
    IDLog("Resetting crop area to default\n");
    crop.c.top=cropcap.defrect.top;
    crop.c.left=cropcap.defrect.left;
    crop.c.width=cropcap.defrect.width;
    crop.c.height=cropcap.defrect.height;
    if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
      perror("VIDIOC_S_CROP");
      cancrop=false;
      /* Errors ignored. */
    }
    if (-1 == xioctl (fd, VIDIOC_G_CROP, &crop)) {
      perror("VIDIOC_G_CROP");
      crop.c.top=-1;
      cancrop=false;
      /* Errors ignored. */
    }
  }
  decoder->usesoftcrop(!cancrop);
  
  // Enumerating capture format
  { struct v4l2_fmtdesc fmt_avail;
    fmt_avail.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //DEBUG(INDI::Logger::DBG_SESSION,"Available Capture Image formats:");
    IDLog("Available Capture Image formats:\n");
    for (fmt_avail.index=0; ioctl(fd, VIDIOC_ENUM_FMT, &fmt_avail) != -1; fmt_avail.index ++) {
      //DEBUGF(INDI::Logger::DBG_SESSION,"\t%d. %s (%c%c%c%c) %s\n", fmt_avail.index, fmt_avail.description, (fmt_avail.pixelformat)&0xFF, (fmt_avail.pixelformat >> 8)&0xFF,
      //     (fmt_avail.pixelformat >> 16)&0xFF, (fmt_avail.pixelformat >> 24)&0xFF, (decoder->issupportedformat(fmt_avail.pixelformat)?"supported":"UNSUPPORTED"));
      IDLog("\t%d. %s (%c%c%c%c) %s\n", fmt_avail.index, fmt_avail.description, (fmt_avail.pixelformat)&0xFF, (fmt_avail.pixelformat >> 8)&0xFF,
	    (fmt_avail.pixelformat >> 16)&0xFF, (fmt_avail.pixelformat >> 24)&0xFF, (decoder->issupportedformat(fmt_avail.pixelformat)?"supported":"UNSUPPORTED"));	    
      {// Enumerating frame sizes available for this pixel format
        struct v4l2_frmsizeenum frm_sizeenum;
        frm_sizeenum.pixel_format=fmt_avail.pixelformat;
        IDLog("\t  Available Frame sizes/rates for this format:\n");
        for (frm_sizeenum.index=0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frm_sizeenum) != -1; frm_sizeenum.index ++) {
          switch (frm_sizeenum.type) {
	  case V4L2_FRMSIZE_TYPE_DISCRETE:
	    IDLog("\t %d. (Discrete)  width %d x height %d\n", frm_sizeenum.index, frm_sizeenum.discrete.width,  frm_sizeenum.discrete.height);
	    break;
          case V4L2_FRMSIZE_TYPE_STEPWISE:
	    IDLog("\t  (Stepwise)  min. width %d, max. width %d step width %d\n", frm_sizeenum.stepwise.min_width,  
		   frm_sizeenum.stepwise.max_width, frm_sizeenum.stepwise.step_width);
	    IDLog("\t  (Stepwise)  min. height %d, max. height %d step height %d, \n", frm_sizeenum.stepwise.min_height,  
		   frm_sizeenum.stepwise.max_height, frm_sizeenum.stepwise.step_height);
	    break;
	  case V4L2_FRMSIZE_TYPE_CONTINUOUS:
	    IDLog("\t  (Continuous--step=1)  min. width %d, max. width %d\n", frm_sizeenum.stepwise.min_width,  
		   frm_sizeenum.stepwise.max_width);
	    IDLog("\t  (Continuous--step=1)  min. height %d, max. height %d \n", frm_sizeenum.stepwise.min_height,  
		   frm_sizeenum.stepwise.max_height);
	    break;
	  default: IDLog("Unknown Frame size type: %d\n",frm_sizeenum.type);
	    break; 
	  }
	  {// Enumerating frame intervals available for this frame size and  this pixel format
	    struct v4l2_frmivalenum frmi_valenum;
	    frmi_valenum.pixel_format=fmt_avail.pixelformat;
	    if (frm_sizeenum.type==V4L2_FRMSIZE_TYPE_DISCRETE) {
	      frmi_valenum.width=frm_sizeenum.discrete.width;
	      frmi_valenum.height=frm_sizeenum.discrete.height;
	    } else {
	      frmi_valenum.width=frm_sizeenum.stepwise.max_width;
	      frmi_valenum.height=frm_sizeenum.stepwise.max_height;
	    }
	    frmi_valenum.type=0; 
	    frmi_valenum.stepwise.min.numerator =0; frmi_valenum.stepwise.min.denominator = 0; 
	    frmi_valenum.stepwise.max.numerator =0; frmi_valenum.stepwise.max.denominator = 0; 
	    frmi_valenum.stepwise.step.numerator =0; frmi_valenum.stepwise.step.denominator = 0; 
	    IDLog("\t    Frame intervals:");
	    for (frmi_valenum.index=0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmi_valenum) != -1; frmi_valenum.index ++) {
	      switch (frmi_valenum.type) {
	      case V4L2_FRMIVAL_TYPE_DISCRETE:
		IDLog("%d/%d s, ", frmi_valenum.discrete.numerator,  frmi_valenum.discrete.denominator);
		break;
	      case V4L2_FRMIVAL_TYPE_STEPWISE:
		IDLog("(Stepwise)  min. %d/%ds, max. %d / %d s, step %d / %d s", frmi_valenum.stepwise.min.numerator, frmi_valenum.stepwise.min.denominator, 
		      frmi_valenum.stepwise.max.numerator, frmi_valenum.stepwise.max.denominator,
		      frmi_valenum.stepwise.step.numerator, frmi_valenum.stepwise.step.denominator);
		break;
	      case V4L2_FRMIVAL_TYPE_CONTINUOUS:
		IDLog("(Continuous)  min. %d / %d s, max. %d / %d s", frmi_valenum.stepwise.min.numerator, 
		      frmi_valenum.stepwise.min.denominator, 
		      frmi_valenum.stepwise.max.numerator, frmi_valenum.stepwise.max.denominator);
		break;
	      default: IDLog("\t    Unknown Frame rate type: %d\n",frmi_valenum.type);
		break; 
	      }
	    }
	    if (frmi_valenum.index==0) {
	      perror("VIDIOC_ENUM_FRAMEINTERVALS");
	      switch (frmi_valenum.type) {
	      case V4L2_FRMIVAL_TYPE_DISCRETE:
		IDLog("%d/%d s, ", frmi_valenum.discrete.numerator,  frmi_valenum.discrete.denominator);
		break;
	      case V4L2_FRMIVAL_TYPE_STEPWISE:
		IDLog("(Stepwise)  min. %d/%ds, max. %d / %d s, step %d / %d s", frmi_valenum.stepwise.min.numerator, 
		      frmi_valenum.stepwise.min.denominator, 
		      frmi_valenum.stepwise.max.numerator, frmi_valenum.stepwise.max.denominator,
		      frmi_valenum.stepwise.step.numerator, frmi_valenum.stepwise.step.denominator);
		break;
	      case V4L2_FRMIVAL_TYPE_CONTINUOUS:
		IDLog("(Continuous)  min. %d / %d s, max. %d / %d s", frmi_valenum.stepwise.min.numerator, 
		      frmi_valenum.stepwise.min.denominator, 
		      frmi_valenum.stepwise.max.numerator, frmi_valenum.stepwise.max.denominator);
		break;
	      default: IDLog("\t    Unknown Frame rate type: %d\n",frmi_valenum.type);
		break; 
	      }
	    } 
	    IDLog("\n");
	    //IDLog("error %d, %s\n", errno, strerror (errno));
	  }
	}
      }
    }
    if (errno != EINVAL) IDLog("Problem enumerating capture formats");
  }
  

    // CLEAR (fmt);

    //    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    //    if (-1 == xioctl (fd, VIDIOC_G_FMT, &fmt))
    //         return errno_exit ("VIDIOC_G_FMT", errmsg);

    //     fmt.fmt.pix.width       = (width == -1)       ? fmt.fmt.pix.width : width; 
    //     fmt.fmt.pix.height      = (height == -1)      ? fmt.fmt.pix.height : height;
    //     fmt.fmt.pix.pixelformat = (pixelFormat == -1) ? fmt.fmt.pix.pixelformat : pixelFormat;
    //     //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

    //     if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
    //             return errno_exit ("VIDIOC_S_FMT", errmsg);

    //     /* Note VIDIOC_S_FMT may change width and height. */

    // 	/* Buggy driver paranoia. */
    // 	min = fmt.fmt.pix.width * 2;
    // 	if (fmt.fmt.pix.bytesperline < min)
    // 		fmt.fmt.pix.bytesperline = min;
    // 	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    // 	if (fmt.fmt.pix.sizeimage < min)
    // 		fmt.fmt.pix.sizeimage = min;

  /* Let's get the actual size */
  CLEAR(fmt);
  
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  
  if (-1 == xioctl (fd, VIDIOC_G_FMT, &fmt))
    return errno_exit ("VIDIOC_G_FMT", errmsg);
  decoder->setformat(fmt, has_ext_pix_format);
  bpp=decoder->getBpp();
  
  
  /*	DEBUGF(INDI::Logger::DBG_SESSION,"Current capture settings: %dx%d image size, %c%c%c%c (%s) image format",  
	fmt.fmt.pix.width,  fmt.fmt.pix.height, (fmt.fmt.pixelformat)&0xFF, (fmt.fmt.pixelformat >> 8)&0xFF,
	(fmt.fmt.pixelformat >> 16)&0xFF, (fmt.fmt.pixelformat >> 24)&0xFF, (decoder->issupportedformat(fmt.fmt.pixelformat)?"supported":"UNSUPPORTED"));
  */

  return 0;
}

int V4L2_Base::init_device(char *errmsg) {
  //findMinMax();

  // decode allocBuffers();

  lxstate = LX_ACTIVE;
  
  switch (io) {
  case IO_METHOD_READ:
    init_read (fmt.fmt.pix.sizeimage);
    break;
    
  case IO_METHOD_MMAP:
    return init_mmap(errmsg);
    break;
    
  case IO_METHOD_USERPTR:
    init_userp (fmt.fmt.pix.sizeimage);
    break;
  }
  return 0;
}

void V4L2_Base::close_device(void) {
  char errmsg[ERRMSGSIZ];
  uninit_device(errmsg);
  
  if (-1 == close (fd))
    errno_exit ("close", errmsg);
  
  fd = -1;
}

int V4L2_Base::open_device(const char *devpath, char *errmsg) {
  struct stat st; 
  
  strncpy(dev_name, devpath, 64);
  
  if (-1 == stat (dev_name, &st)) {
    fprintf (stderr, "Cannot identify %s: %d, %s\n",
	     dev_name, errno, strerror (errno));
    snprintf(errmsg, ERRMSGSIZ, "Cannot identify %s: %d, %s\n",
	     dev_name, errno, strerror (errno));
    return -1;
  }
  
  if (!S_ISCHR (st.st_mode)) {
    fprintf (stderr, "%s is no device\n", dev_name);
    snprintf(errmsg, ERRMSGSIZ, "%s is no device\n", dev_name);
    return -1;
  }
  
  fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

  if (-1 == fd) {
    fprintf (stderr, "Cannot open %s: %d, %s\n",
	     dev_name, errno, strerror (errno));
    snprintf(errmsg, ERRMSGSIZ, "Cannot open %s: %d, %s\n",
	     dev_name, errno, strerror (errno));
    return -1;
  }
  
  streamedonce=false;
  
  return 0;
}

void V4L2_Base::getinputs(ISwitchVectorProperty *inputssp) {
  struct v4l2_input input_avail;
  ISwitch *inputs=NULL;
  for (input_avail.index=0; ioctl(fd, VIDIOC_ENUMINPUT, &input_avail) != -1;
       input_avail.index ++) {
    //IDLog("\t%d. %s (type %s)\n", input_avail.index, input_avail.name, 
    //	  (input_avail.type==V4L2_INPUT_TYPE_TUNER?"Tuner/RF Demodulator":"Composite/S-Video"));
    
    inputs = (inputs==NULL)?(ISwitch *)malloc(sizeof(ISwitch)):
      (ISwitch *) realloc (inputs, (input_avail.index+1) * sizeof (ISwitch));
    strncpy(inputs[input_avail.index].name, (const char *)input_avail.name , MAXINDINAME);
    strncpy(inputs[input_avail.index].label,(const char *)input_avail.name, MAXINDILABEL);
    
  }
  if (errno != EINVAL) IDLog("\tProblem enumerating inputs");
  inputssp->sp=inputs;
  inputssp->nsp=input_avail.index;
  if (-1 == ioctl (fd, VIDIOC_G_INPUT, &input.index)) {
    perror ("VIDIOC_G_INPUT");
    exit (EXIT_FAILURE);
  }
  IUResetSwitch(inputssp);
  inputs[input.index].s=ISS_ON;
  IDLog("Current Video input(%d.): %s\n", input.index, inputs[input.index].name);
  //IDSetSwitch(inputssp, "Current input: %d. %s", input.index, inputs[input.index].name);
}

int V4L2_Base::setinput(unsigned int inputindex, char *errmsg) {
  IDLog("Setting Video input to %d\n", inputindex);
  if (streamedonce) {
    close_device();
    open_device(path, errmsg);
  }
  if (-1 == ioctl (fd, VIDIOC_S_INPUT, &inputindex)) {
    return errno_exit ("VIDIOC_S_INPUT", errmsg);
  }
  if (-1 == ioctl (fd, VIDIOC_G_INPUT, &input.index)) {
    return errno_exit ("VIDIOC_G_INPUT", errmsg);
  }
  //decode  reallocate_buffers=true;
  return 0;
}

void V4L2_Base::getcaptureformats(ISwitchVectorProperty *captureformatssp) {
  struct v4l2_fmtdesc fmt_avail;
  ISwitch *formats=NULL;
  unsigned int i;//, initial;
  if (captureformatssp->sp) free(captureformatssp->sp);
  fmt_avail.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  // IDLog("Available Capture Image formats:\n");
  for (fmt_avail.index=0; ioctl(fd, VIDIOC_ENUM_FMT, &fmt_avail) != -1;
       fmt_avail.index ++) {
    formats = (formats==NULL)?(ISwitch *)malloc(sizeof(ISwitch)):
      (ISwitch *) realloc (formats, (fmt_avail.index+1) * sizeof (ISwitch));
    strncpy(formats[fmt_avail.index].name, (const char *)fmt_avail.description , MAXINDINAME);
    strncpy(formats[fmt_avail.index].label,(const char *)fmt_avail.description, MAXINDILABEL);
    //assert(sizeof(void *) == sizeof(int));
    formats[fmt_avail.index].aux=(int *)malloc(sizeof(int));
    *(int *)(formats[fmt_avail.index].aux) = fmt_avail.pixelformat;
    //IDLog("\t%s (%c%c%c%c)\n", fmt_avail.description, (fmt_avail.pixelformat >> 24)&0xFF,
    //	  (fmt_avail.pixelformat >> 16)&0xFF, (fmt_avail.pixelformat >> 8)&0xFF, 
    //	  (fmt_avail.pixelformat)&0xFF);
  }
  captureformatssp->sp=formats;
  captureformatssp->nsp=fmt_avail.index;
  
  CLEAR (fmt);
  fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl (fd, VIDIOC_G_FMT, &fmt)) {
    perror ("VIDIOC_G_FMT");
    exit (EXIT_FAILURE);
  }
  IUResetSwitch(captureformatssp);
  for (i=0; i < fmt_avail.index; i++) {
    formats[i].s=ISS_OFF;
    if (fmt.fmt.pix.pixelformat == *((int *) (formats[i].aux))) {
      formats[i].s=ISS_ON;
      //initial=i;
      IDLog("Current Capture format is (%d.) %c%c%c%c.\n", i, (fmt.fmt.pix.pixelformat)&0xFF, (fmt.fmt.pix.pixelformat >> 8)&0xFF,
	    (fmt.fmt.pix.pixelformat >> 16)&0xFF, (fmt.fmt.pix.pixelformat >> 24)&0xFF);
      //break;
    }
  }
  //IDSetSwitch(captureformatssp, "Capture format: %d. %s", initial, formats[initial].name);
}

int V4L2_Base::setcaptureformat(unsigned int captureformat, char *errmsg) {
  unsigned int oldformat;
  oldformat = fmt.fmt.pix.pixelformat;
  fmt.fmt.pix.pixelformat = captureformat;
  if (streamedonce) {
    close_device();
    open_device(path ,errmsg);
  }
    //     //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
  if (-1 == xioctl (fd, VIDIOC_TRY_FMT, &fmt)) {
    fmt.fmt.pix.pixelformat = oldformat;
    return errno_exit ("VIDIOC_TRY_FMT", errmsg);
  }
  if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt)) {
    return errno_exit ("VIDIOC_S_FMT", errmsg);
  }
  //decode reallocate_buffers=true;
  decoder->setformat(fmt, has_ext_pix_format);
  bpp=decoder->getBpp();
  return 0;
}

void V4L2_Base::getcapturesizes(ISwitchVectorProperty *capturesizessp, INumberVectorProperty *capturesizenp) {
  struct v4l2_frmsizeenum frm_sizeenum;
  ISwitch *sizes=NULL;
  INumber *sizevalue=NULL;
  bool sizefound=false;
  if (capturesizessp->sp) free(capturesizessp->sp);
  if (capturesizenp->np) free(capturesizenp->np);

  frm_sizeenum.pixel_format=fmt.fmt.pix.pixelformat;
  //IDLog("\t  Available Frame sizes/rates for this format:\n");
  for (frm_sizeenum.index=0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frm_sizeenum) != -1;
       frm_sizeenum.index ++) {
    switch (frm_sizeenum.type) {
    case V4L2_FRMSIZE_TYPE_DISCRETE:
      sizes = (sizes==NULL)?(ISwitch *)malloc(sizeof(ISwitch)):
      (ISwitch *) realloc (sizes, (frm_sizeenum.index+1) * sizeof (ISwitch));
 
      snprintf(sizes[frm_sizeenum.index].name, MAXINDINAME, "%dx%d", frm_sizeenum.discrete.width,  frm_sizeenum.discrete.height);
      snprintf(sizes[frm_sizeenum.index].label, MAXINDINAME, "%dx%d", frm_sizeenum.discrete.width,  frm_sizeenum.discrete.height);
      sizes[frm_sizeenum.index].s=ISS_OFF;
      if (!sizefound) {
	if ((fmt.fmt.pix.width == frm_sizeenum.discrete.width) && (fmt.fmt.pix.height == frm_sizeenum.discrete.height)) {
	  sizes[frm_sizeenum.index].s=ISS_ON;
	  sizefound=true;
	  IDLog("Current capture size is (%d.)  %dx%d\n", frm_sizeenum.index, frm_sizeenum.discrete.width,  frm_sizeenum.discrete.height);
	}
      }
      break;
    case V4L2_FRMSIZE_TYPE_STEPWISE:
    case V4L2_FRMSIZE_TYPE_CONTINUOUS:
      sizevalue=(INumber *)malloc(2 * sizeof(INumber));
      IUFillNumber(sizevalue, "Width", "Width", "%.0f", frm_sizeenum.stepwise.min_width, frm_sizeenum.stepwise.max_width, frm_sizeenum.stepwise.step_width, fmt.fmt.pix.width);
      IUFillNumber(sizevalue+1, "Height", "Height", "%.0f", frm_sizeenum.stepwise.min_height, frm_sizeenum.stepwise.max_height, frm_sizeenum.stepwise.step_height, fmt.fmt.pix.height);
	IDLog("Current capture size is %dx%d\n", fmt.fmt.pix.width,  fmt.fmt.pix.height);
      break;
    default: IDLog("Unknown Frame size type: %d\n",frm_sizeenum.type);
      break; 
    }
  }

  if (sizes != NULL) {
    capturesizessp->sp=sizes;
    capturesizessp->nsp=frm_sizeenum.index;
    capturesizenp->np=NULL;
  } else {
    capturesizenp->np=sizevalue;
    capturesizenp->nnp=2;
    capturesizessp->sp=NULL;
  }
}

int V4L2_Base::setcapturesize(unsigned int w, unsigned int h, char *errmsg) {
  unsigned int oldw, oldh;
  oldw = fmt.fmt.pix.width;
  oldh = fmt.fmt.pix.height;
  fmt.fmt.pix.width = w;
  fmt.fmt.pix.height = h;
    //     //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
  if (streamedonce) {
    close_device();
    open_device(path, errmsg);
  }
  if (-1 == xioctl (fd, VIDIOC_TRY_FMT, &fmt)) {
    fmt.fmt.pix.width = oldw;
    fmt.fmt.pix.height = oldh;
    return errno_exit ("VIDIOC_TRY_FMT", errmsg);
  }
  if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt)) {
      fmt.fmt.pix.width = oldw;
      fmt.fmt.pix.height = oldh;
      return errno_exit ("VIDIOC_S_FMT", errmsg);
  }
  // Drivers may change sizes
  if (-1 == xioctl (fd, VIDIOC_G_FMT, &fmt)) {
      fmt.fmt.pix.width = oldw;
      fmt.fmt.pix.height = oldh;
    return errno_exit ("VIDIOC_G_FMT", errmsg);
  }
  decoder->setformat(fmt, has_ext_pix_format);
  bpp=decoder->getBpp();
  //decode reallocate_buffers=true;
  //decode cropset=false;
  //decode allocBuffers();
  return 0;
}

void V4L2_Base::getframerates(ISwitchVectorProperty *frameratessp, INumberVectorProperty *frameratenp) {
  struct v4l2_frmivalenum frmi_valenum;
  ISwitch *rates=NULL;
  INumber *ratevalue=NULL;
  struct v4l2_fract frate;

  if (frameratessp->sp) free(frameratessp->sp);
  if (frameratenp->np) free(frameratenp->np);
  frate=(this->*getframerate)();

  bzero(&frmi_valenum, sizeof(frmi_valenum));
  frmi_valenum.pixel_format=fmt.fmt.pix.pixelformat;
  frmi_valenum.width=fmt.fmt.pix.width;
  frmi_valenum.height=fmt.fmt.pix.height;
  
  for (frmi_valenum.index=0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmi_valenum) != -1;
       frmi_valenum.index ++) {
    switch (frmi_valenum.type) {
    case V4L2_FRMIVAL_TYPE_DISCRETE:
      rates = (rates==NULL)?(ISwitch *)malloc(sizeof(ISwitch)):
      (ISwitch *) realloc (rates, (frmi_valenum.index+1) * sizeof (ISwitch));
      snprintf(rates[frmi_valenum.index].name, MAXINDINAME, "%d/%d", frmi_valenum.discrete.numerator,  frmi_valenum.discrete.denominator);
      snprintf(rates[frmi_valenum.index].label, MAXINDINAME, "%d/%d", frmi_valenum.discrete.numerator,  frmi_valenum.discrete.denominator);
      if ((frate.numerator==frmi_valenum.discrete.numerator) &&(frate.denominator==frmi_valenum.discrete.denominator)) {
	IDLog("Current frame interval is %d/%d\n", frmi_valenum.discrete.numerator, frmi_valenum.discrete.denominator);
	rates[frmi_valenum.index].s=ISS_ON;
      }
      else
	rates[frmi_valenum.index].s=ISS_OFF;
      break;
    case V4L2_FRMIVAL_TYPE_STEPWISE:
    case V4L2_FRMIVAL_TYPE_CONTINUOUS:
      ratevalue=(INumber *)malloc(sizeof(INumber));
      IUFillNumber(ratevalue, "V4L2_FRAME_INTERVAL", "Frame Interval", "%.0f", frmi_valenum.stepwise.min.numerator/(double)frmi_valenum.stepwise.min.denominator, frmi_valenum.stepwise.max.numerator/(double)frmi_valenum.stepwise.max.denominator, frmi_valenum.stepwise.step.numerator/(double)frmi_valenum.stepwise.step.denominator, frate.numerator/(double)frate.denominator);
      break;
    default: IDLog("Unknown Frame rate type: %d\n",frmi_valenum.type);
      break; 
    }
  }
  frameratessp->sp=NULL; frameratessp->nsp=0;
  frameratenp->np=NULL; frameratenp->nnp=0;
  if (frmi_valenum.index!=0) {
    if (rates != NULL) {
      frameratessp->sp=rates;
      frameratessp->nsp=frmi_valenum.index;
    } else {
      frameratenp->np=ratevalue;
      frameratenp->nnp=1;
    }
  }
}

int V4L2_Base::setcroprect(int x, int y, int w, int h, char *errmsg) {
  struct v4l2_crop oldcrop=crop;
  bool softcrop;

  crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  crop.c.left = x; crop.c.top = y; crop.c.width = w; crop.c.height = h;
  if (crop.c.left + crop.c.width > fmt.fmt.pix.width) {
    strncpy(errmsg, "crop width exceeds image width",ERRMSGSIZ);
    return -1;
  }
  if (crop.c.top + crop.c.height > fmt.fmt.pix.height) {
    strncpy(errmsg, "crop height exceeds image height",ERRMSGSIZ);
    return -1;
  }
  // we only support pair sizes
  if ((crop.c.width % 2 != 0) || (crop.c.height % 2 != 0)) {
      strncpy(errmsg, "crop width/height must be pair",ERRMSGSIZ);
    return -1;
  }
  if ((crop.c.left==0)&&(crop.c.top==0)&&(crop.c.width==fmt.fmt.pix.width)&&(crop.c.height==fmt.fmt.pix.height)) {
    cropset=false;
    decoder->resetcrop();
  } else {
    if (cancrop) {
      if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
	return errno_exit ("VIDIOC_S_CROP", errmsg);
      }
      if (-1 == xioctl (fd, VIDIOC_G_CROP, &crop)) {
	return errno_exit ("VIDIOC_G_CROP", errmsg);
      } 
    }
    softcrop=decoder->setcrop(crop);
    cropset=true;
    if ((!cancrop) && (!softcrop)) {
      cropset=false;
      strncpy(errmsg, "No hardware and sofwtare cropping for this format",ERRMSGSIZ);
      return -1;
    }

  }
  //decode allocBuffers();
  IDLog("V4L2 base setcroprect %dx%d at (%d, %d)\n", crop.c.width, crop.c.height, crop.c.left, crop.c.top); 
  return 0;
}

int V4L2_Base::getWidth()
{
  if (cropset)
    return crop.c.width;
  else
    return fmt.fmt.pix.width;
}

int V4L2_Base::getHeight()
{
  if (cropset)
    return crop.c.height;
  else
 return fmt.fmt.pix.height;
}

int V4L2_Base::getBpp()
{
  return bpp;
}

int V4L2_Base::getFormat()
{
  return fmt.fmt.pix.pixelformat;
}

struct v4l2_rect V4L2_Base::getcroprect()
{
  return crop.c;
}

int V4L2_Base::stdsetframerate(struct v4l2_fract frate, char *errmsg)
{
  struct v4l2_streamparm sparm;
  //if (!cansetrate) {sprintf(errmsg, "Can not set rate"); return -1;}
  bzero(&sparm, sizeof(struct v4l2_streamparm));
  sparm.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
  sparm.parm.capture.timeperframe=frate;
  if (-1 == xioctl (fd, VIDIOC_S_PARM, &sparm)) {
    //cansetrate=false;
    return errno_exit("VIDIOC_S_PARM", errmsg);
  }
  return 0;
}

int V4L2_Base::pwcsetframerate(struct v4l2_fract frate, char *errmsg) {
  int fps= frate.denominator / frate.numerator;
  fmt.fmt.pix.priv |= (fps << PWC_FPS_SHIFT);
    if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt)) {
    return errno_exit ("pwcsetframerate", errmsg);
  }
    frameRate=frate;
    return 0;
}

struct v4l2_fract V4L2_Base::stdgetframerate()
{
  struct v4l2_streamparm sparm;
  //if (!cansetrate) return frameRate;
  bzero(&sparm, sizeof(struct v4l2_streamparm));
  sparm.type= V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl (fd, VIDIOC_G_PARM, &sparm)) {
    perror ("VIDIOC_G_PARM");
  } else {
    frameRate=sparm.parm.capture.timeperframe;
  }
  //if (!(sparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) cansetrate=false;

  return frameRate;
}

char * V4L2_Base::getDeviceName()
{
  return ((char *) cap.card);
}

void V4L2_Base::getMaxMinSize(int & x_max, int & y_max, int & x_min, int & y_min)
{
  x_max = xmax; y_max = ymax; x_min = xmin; y_min = ymin;
}

int V4L2_Base::setSize(int x, int y)
{
   char errmsg[ERRMSGSIZ];
   int oldW, oldH;
 
   oldW = fmt.fmt.pix.width;
   oldH = fmt.fmt.pix.height;

   fmt.fmt.pix.width  = x;
   fmt.fmt.pix.height = y;
  if (streamedonce) {
    close_device();
    open_device(path, errmsg);
  }
   if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
   {
        fmt.fmt.pix.width  = oldW;
        fmt.fmt.pix.height = oldH;
        return errno_exit ("VIDIOC_S_FMT", errmsg);
   }

   /* PWC bug? It seems that setting the "wrong" width and height will mess something in the driver.
      Only 160x120, 320x280, and 640x480 are accepted. If I try to set it for example to 300x200, it wii
      get set to 320x280, which is fine, but then the video information is messed up for some reason. */
   //   xioctl (fd, VIDIOC_S_FMT, &fmt);
 
  
   //allocBuffers();

  return 0;
}

void V4L2_Base::setColorProcessing(bool quantization, bool colorconvert, bool linearization) 
{
  decoder->setQuantization(quantization);
  decoder->setLinearization(linearization);
  bpp=decoder->getBpp();
}

unsigned char * V4L2_Base::getY()
{
  return decoder->getY();
}

unsigned char * V4L2_Base::getU()
{
  return decoder->getU();
}

unsigned char * V4L2_Base::getV()
{
  return decoder->getV();
}

unsigned char * V4L2_Base::getColorBuffer()
{
  return decoder->getColorBuffer();
}

unsigned char * V4L2_Base::getRGBBuffer()
{
  return decoder->getRGBBuffer();
}

float * V4L2_Base::getLinearY()
{
  return decoder->getLinearY();
}


void V4L2_Base::registerCallback(WPF *fp, void *ud)
{
  callback = fp;
  uptr = ud;
}

void V4L2_Base::findMinMax()
{
    char errmsg[ERRMSGSIZ];
    struct v4l2_format tryfmt;
    CLEAR(tryfmt);

    xmin = xmax = fmt.fmt.pix.width;
    ymin = ymax = fmt.fmt.pix.height;

    tryfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tryfmt.fmt.pix.width       = 10;
    tryfmt.fmt.pix.height      = 10;
    tryfmt.fmt.pix.pixelformat = fmt.fmt.pix.pixelformat;
    tryfmt.fmt.pix.field       = fmt.fmt.pix.field;

    if (-1 == xioctl (fd, VIDIOC_TRY_FMT, &tryfmt))
    {
        errno_exit ("VIDIOC_TRY_FMT 1", errmsg);
        return;
    }
   
    xmin = tryfmt.fmt.pix.width;
    ymin = tryfmt.fmt.pix.height;

    tryfmt.fmt.pix.width       = 1600;
    tryfmt.fmt.pix.height      = 1200;

    if (-1 == xioctl (fd, VIDIOC_TRY_FMT, &tryfmt))
    {
                errno_exit ("VIDIOC_TRY_FMT 2", errmsg);
                return;
    }

    xmax = tryfmt.fmt.pix.width;
    ymax = tryfmt.fmt.pix.height;

    cerr << "Min X: " << xmin << " - Max X: " << xmax << " - Min Y: " << ymin << " - Max Y: " << ymax << endl;
}

void V4L2_Base::enumerate_ctrl (void)
{
  char errmsg[ERRMSGSIZ];
  CLEAR(queryctrl);

  for (queryctrl.id = V4L2_CID_BASE; queryctrl.id < V4L2_CID_LASTP1; queryctrl.id++)
  {
    if (0 == xioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
      {
	if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
	  cerr << "DISABLED--Control " << queryctrl.name << endl;
	  continue;
	}
	cerr << "Control " << queryctrl.name << endl;
	
	if ((queryctrl.type == V4L2_CTRL_TYPE_MENU))//|| (queryctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU)) // not in < 3.5
	  enumerate_menu ();
	if (queryctrl.type == V4L2_CTRL_TYPE_BOOLEAN)
	  cerr << "  boolean" <<endl;
	if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
	  cerr << "  integer" <<endl;
	//if (queryctrl.type == V4L2_CTRL_TYPE_BITMASK) //not in < 3.1
	//  cerr << "  bitmask" <<endl;	
	if (queryctrl.type == V4L2_CTRL_TYPE_BUTTON)
	  cerr << "  button" <<endl;
      } else
      {
	if (errno == EINVAL)
	  continue;
	
	errno_exit("VIDIOC_QUERYCTRL", errmsg);
	return;
      }
  }
  
  for (queryctrl.id = V4L2_CID_PRIVATE_BASE;  ;  queryctrl.id++)
  {
    if (0 == xioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
      {
	
	if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
	  cerr << "DISABLED--Private Control " << queryctrl.name << endl;
	  continue;
	}
	
	cerr << "Private Control " << queryctrl.name << endl;
	
	if ((queryctrl.type == V4L2_CTRL_TYPE_MENU))// || (queryctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU))
	  enumerate_menu ();
	if (queryctrl.type == V4L2_CTRL_TYPE_BOOLEAN)
	  cerr << "  boolean" <<endl;
	if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
	  cerr << "  integer" <<endl;
	//if (queryctrl.type == V4L2_CTRL_TYPE_BITMASK)
	//  cerr << "  bitmask" <<endl;	
	if (queryctrl.type == V4L2_CTRL_TYPE_BUTTON)
	  cerr << "  button" <<endl;
      } else {
	  if (errno == EINVAL)
	    break;

	  errno_exit ("VIDIOC_QUERYCTRL", errmsg);
	  return;
        }

  }

}

void V4L2_Base::enumerate_menu (void)
{
  cerr << "  Menu items:" << endl;

  CLEAR(querymenu);
  querymenu.id = queryctrl.id;
  
  for (querymenu.index = queryctrl.minimum;  querymenu.index <= queryctrl.maximum; querymenu.index++)
    {
      if (0 == xioctl (fd, VIDIOC_QUERYMENU, &querymenu))
	{
	  cerr << "  " <<  querymenu.name << endl;
	} 
      //else
      //{
      //  errno_exit("VIDIOC_QUERYMENU", errmsg);
      //  return;
      //}
    }
}

int  V4L2_Base::query_ctrl(unsigned int ctrl_id, double & ctrl_min, double & ctrl_max, double & ctrl_step, double & ctrl_value, char *errmsg)
{ 

    struct v4l2_control control;

    CLEAR(queryctrl);

    queryctrl.id = ctrl_id;

   if (-1 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
   {
        if (errno != EINVAL) 
                return errno_exit ("VIDIOC_QUERYCTRL", errmsg);
                
        else 
        {
                cerr << "#" << ctrl_id << " is not supported" << endl;
                snprintf(errmsg, ERRMSGSIZ, "# %d is not supported", ctrl_id);
		return -1;
        }
   } else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
   {
        cerr << "#" << ctrl_id << " is disabled" << endl;
        snprintf(errmsg, ERRMSGSIZ, "# %d is disabled", ctrl_id);
        return -1;
   }

   ctrl_min   = queryctrl.minimum;
   ctrl_max   = queryctrl.maximum;
   ctrl_step  = queryctrl.step;
   ctrl_value = queryctrl.default_value;

   /* Get current value */
   CLEAR(control);
   control.id = ctrl_id;

   if (0 == xioctl(fd, VIDIOC_G_CTRL, &control))
      ctrl_value = control.value;

    cerr << queryctrl.name << " -- min: " << ctrl_min << " max: " << ctrl_max << " step: " << ctrl_step << " value: " << ctrl_value << endl;

    return 0;

}

void  V4L2_Base::queryControls(INumberVectorProperty *nvp, unsigned int *nnumber, ISwitchVectorProperty **options, unsigned int *noptions, const char *dev, const char *group) {
  struct v4l2_control control;
  
  INumber *numbers = NULL;
  unsigned int *num_ctrls = NULL;
  int nnum=0;
  ISwitchVectorProperty *opt=NULL;
  unsigned int nopt=0;
  char optname[]="OPT000";
  char swonname[]="SET_OPT000";
  char swoffname[]="UNSET_OPT000";
  char menuname[]="MENU000";
  char menuoptname[]="MENU000_OPT000";
  *noptions = 0;
  *nnumber = 0;
  
  CLEAR(queryctrl);

  for (queryctrl.id = V4L2_CID_BASE; queryctrl.id < V4L2_CID_LASTP1; queryctrl.id++)
    {
      if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
        {
	  if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
	    {
	      cerr << queryctrl.name << " is disabled." << endl;
	      continue;
	    }
	  
	  if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
	    {
	      numbers = (numbers == NULL) ? (INumber *) malloc (sizeof(INumber)) :
		(INumber *) realloc (numbers, (nnum+1) * sizeof (INumber));
	      
	      num_ctrls = (num_ctrls == NULL) ? (unsigned int *) malloc  (sizeof (unsigned int)) :
		(unsigned int *) realloc (num_ctrls, (nnum+1) * sizeof (unsigned int));
	      
	      strncpy(numbers[nnum].name, (const char *)entityXML((char *) queryctrl.name) , MAXINDINAME);
	      strncpy(numbers[nnum].label, (const char *)entityXML((char *) queryctrl.name), MAXINDILABEL);
	      strncpy(numbers[nnum].format, "%0.f", MAXINDIFORMAT);
	      numbers[nnum].min    = queryctrl.minimum;
	      numbers[nnum].max    = queryctrl.maximum;
	      numbers[nnum].step   = queryctrl.step;
	      numbers[nnum].value  = queryctrl.default_value;
	      
	      /* Get current value if possible */
	      CLEAR(control);
	      control.id = queryctrl.id;
	      if (0 == xioctl(fd, VIDIOC_G_CTRL, &control))
		numbers[nnum].value = control.value;
	      
	      /* Store ID info in INumber. This is the first time ever I make use of aux0!! */
	      num_ctrls[nnum] = queryctrl.id;
	      
	      cerr << "Adding " << queryctrl.name << " -- min: " << queryctrl.minimum << " max: " << queryctrl.maximum << 
		" step: " << queryctrl.step << " value: " << numbers[nnum].value << endl;
	      
	      nnum++;
              
	    }
	  if (queryctrl.type == V4L2_CTRL_TYPE_BOOLEAN)
	    {
	      ISwitch *sw = (ISwitch *)malloc(2*sizeof(ISwitch));
	      snprintf(optname+3, 4, "%03d", nopt);
	      snprintf(swonname+7, 4, "%03d", nopt);
	      snprintf(swoffname+9, 4, "%03d", nopt);
	      
	      opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
		(ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));
	      
	      CLEAR(control);
	      control.id = queryctrl.id;
	      xioctl(fd, VIDIOC_G_CTRL, &control);

	      IUFillSwitch(sw, swonname, "Off", (control.value?ISS_OFF:ISS_ON));
	      IUFillSwitch(sw+1, swoffname, "On", (control.value?ISS_ON:ISS_OFF));
	      queryctrl.name[31]='\0';
	      IUFillSwitchVector (&opt[nopt], sw, 2, dev, optname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_1OFMANY, 0.0, IPS_IDLE);
              opt[nopt].aux=malloc(sizeof(unsigned int));
	      *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);

	      IDLog("Adding switch  %s (%s)\n", queryctrl.name, (control.value?"On":"Off"));
	      nopt += 1;
	    }
	  if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
	    {
	      ISwitch *sw = NULL;
	      unsigned int nmenuopt=0;
	      char sname[32];
	      snprintf(menuname+4, 4, "%03d", nopt);
	      snprintf(menuoptname+4, 4, "%03d", nopt);
	      menuoptname[7]='_';
	      
	      opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
		(ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));

	      CLEAR(control);
	      control.id = queryctrl.id;
	      xioctl(fd, VIDIOC_G_CTRL, &control);

	      CLEAR(querymenu);
	      querymenu.id = queryctrl.id;
	      
	      for (querymenu.index = queryctrl.minimum;  querymenu.index <= queryctrl.maximum; querymenu.index++)
		{
		  if (0 == xioctl (fd, VIDIOC_QUERYMENU, &querymenu))
		    {
		      sw = (sw == NULL) ? (ISwitch *) malloc (sizeof(ISwitch)) :
		      (ISwitch *) realloc (sw, (nmenuopt+1) * sizeof (ISwitch));
		      snprintf(menuoptname+11, 4, "%03d", nmenuopt);
		      snprintf(sname, 31, "%s", querymenu.name);
		      sname[31]='\0';
		      IDLog("Adding menu item %s %s %s item %d \n", querymenu.name, sname, menuoptname, nmenuopt);
		      //IUFillSwitch(&sw[nmenuopt], menuoptname, (const char *)sname, (control.value==nmenuopt?ISS_ON:ISS_OFF));
		      IUFillSwitch(&sw[nmenuopt], menuoptname, (const char *)entityXML((char *)querymenu.name), (control.value==nmenuopt?ISS_ON:ISS_OFF));
		      nmenuopt+=1;
		    } else
		    {
		      //errno_exit("VIDIOC_QUERYMENU", errmsg);
		      //exit(3);
		    }
		}
	      
	      queryctrl.name[31]='\0';
	      IUFillSwitchVector (&opt[nopt], sw, nmenuopt, dev, menuname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_1OFMANY, 0.0, IPS_IDLE);
	      opt[nopt].aux=malloc(sizeof(unsigned int));
	      *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);

	      IDLog("Adding menu  %s (item %d set)\n", queryctrl.name, control.value);
	      nopt += 1;
	    }
        } else { 
	if (errno != EINVAL) {
	  if(numbers) free(numbers);
	  if (opt) free(opt);
	  perror("VIDIOC_QUERYCTRL");
	  return;
	}
      }
    }

  for (queryctrl.id = V4L2_CID_PRIVATE_BASE;  ;  queryctrl.id++)
  {
    if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
      {
	if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
	  {
	    cerr << queryctrl.name << " is disabled." << endl;
	    continue;
	  }
	
	if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
	  {
	    numbers = (numbers == NULL) ? (INumber *) malloc (sizeof(INumber)) :
	      (INumber *) realloc (numbers, (nnum+1) * sizeof (INumber));
	    
	    num_ctrls = (num_ctrls == NULL) ? (unsigned int *) malloc  (sizeof (unsigned int)) :
	      (unsigned int *) realloc (num_ctrls, (nnum+1) * sizeof (unsigned int));
	    
	    strncpy(numbers[nnum].name, (const char *)entityXML((char *) queryctrl.name) , MAXINDINAME);
	    strncpy(numbers[nnum].label, (const char *)entityXML((char *) queryctrl.name), MAXINDILABEL);
	    strncpy(numbers[nnum].format, "%0.f", MAXINDIFORMAT);
	    numbers[nnum].min    = queryctrl.minimum;
	    numbers[nnum].max    = queryctrl.maximum;
	    numbers[nnum].step   = queryctrl.step;
	    numbers[nnum].value  = queryctrl.default_value;
	    
	    /* Get current value if possible */
	    CLEAR(control);
	    control.id = queryctrl.id;
	    if (0 == xioctl(fd, VIDIOC_G_CTRL, &control))
	      numbers[nnum].value = control.value;
	    
	    /* Store ID info in INumber. This is the first time ever I make use of aux0!! */
	    num_ctrls[nnum] = queryctrl.id;
	      cerr << "Adding ext. " << queryctrl.name << " -- min: " << queryctrl.minimum << " max: " << queryctrl.maximum << 
		" step: " << queryctrl.step << " value: " << numbers[nnum].value << endl;
	      	    
	    nnum++;
            
	  }
	  if (queryctrl.type == V4L2_CTRL_TYPE_BOOLEAN)
	    {
	      ISwitch *sw = (ISwitch *)malloc(2*sizeof(ISwitch));
	      snprintf(optname+3, 4, "%03d", nopt);
	      snprintf(swonname+7, 4, "%03d", nopt);
	      snprintf(swoffname+9, 4, "%03d", nopt);
	      
	      opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
		(ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));
	      
	      CLEAR(control);
	      control.id = queryctrl.id;
	      xioctl(fd, VIDIOC_G_CTRL, &control);

	      IUFillSwitch(sw, swonname, "On", (control.value?ISS_ON:ISS_OFF));
	      IUFillSwitch(sw+1, swoffname, "Off", (control.value?ISS_OFF:ISS_ON));
	      queryctrl.name[31]='\0';
	      IUFillSwitchVector (&opt[nopt], sw, 2, dev, optname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_1OFMANY, 0.0, IPS_IDLE);

	      opt[nopt].aux=malloc(sizeof(unsigned int));
	      *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);

	      IDLog("Adding ext. switch  %s (%s)\n", queryctrl.name, (control.value?"On":"Off"));
	      nopt += 1;
	    }
	  if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
	    {
	      ISwitch *sw = NULL;
	      unsigned int nmenuopt=0;
	      char sname[32];
	      snprintf(menuname+4, 4, "%03d", nopt);
	      snprintf(menuoptname+4, 4, "%03d", nopt);
	      menuoptname[7]='_';
	      
	      opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
		(ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));

	      CLEAR(control);
	      control.id = queryctrl.id;
	      xioctl(fd, VIDIOC_G_CTRL, &control);

	      CLEAR(querymenu);
	      querymenu.id = queryctrl.id;
	      
	      for (querymenu.index = queryctrl.minimum;  querymenu.index <= queryctrl.maximum; querymenu.index++)
		{
		  if (0 == xioctl (fd, VIDIOC_QUERYMENU, &querymenu))
		    {
		      sw = (sw == NULL) ? (ISwitch *) malloc (sizeof(ISwitch)) :
		      (ISwitch *) realloc (sw, (nmenuopt+1) * sizeof (ISwitch));
		      snprintf(menuoptname+11, 4, "%03d", nmenuopt);
		      snprintf(sname, 31, "%s", querymenu.name);
		      sname[31]='\0';
		      IDLog("Adding menu item %s %s %s item %d \n", querymenu.name, sname, menuoptname, nmenuopt);
		      //IUFillSwitch(&sw[nmenuopt], menuoptname, (const char *)sname, (control.value==nmenuopt?ISS_ON:ISS_OFF));
		      IUFillSwitch(&sw[nmenuopt], menuoptname, (const char *)entityXML((char *)querymenu.name), (control.value==nmenuopt?ISS_ON:ISS_OFF));
		      nmenuopt+=1;
		    } else
		    {
		      //errno_exit("VIDIOC_QUERYMENU", errmsg);
		      //exit(3);
		    }
		}
	      
	      queryctrl.name[31]='\0';
	      IUFillSwitchVector (&opt[nopt], sw, nmenuopt, dev, menuname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_1OFMANY, 0.0, IPS_IDLE);

	      opt[nopt].aux=malloc(sizeof(unsigned int));
	      *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);

	      IDLog("Adding ext. menu  %s (item %d set)\n", queryctrl.name, control.value);
	      nopt += 1;
	    }

      }
    else break;
  } 
  
  /* Store numbers in aux0 */
  for (int i=0; i < nnum; i++) 
    numbers[i].aux0 = &num_ctrls[i];
  
  nvp->np  = numbers;
  nvp->nnp = nnum;
  *nnumber=nnum;
  
  *options=opt;
  *noptions=nopt;

}


int  V4L2_Base::queryINTControls(INumberVectorProperty *nvp)
{
   struct v4l2_control control;
   char errmsg[ERRMSGSIZ];
   CLEAR(queryctrl);
   INumber *numbers = NULL;
   unsigned int *num_ctrls = NULL;
   int nnum=0;

  for (queryctrl.id = V4L2_CID_BASE; queryctrl.id < V4L2_CID_LASTP1; queryctrl.id++)
  {
        if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
        {
                if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
                {
                    cerr << queryctrl.name << " is disabled." << endl;
                    continue;
                }

                if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
                {
                   numbers = (numbers == NULL) ? (INumber *) malloc (sizeof(INumber)) :
                                                 (INumber *) realloc (numbers, (nnum+1) * sizeof (INumber));

                   num_ctrls = (num_ctrls == NULL) ? (unsigned int *) malloc  (sizeof (unsigned int)) :
                                                     (unsigned int *) realloc (num_ctrls, (nnum+1) * sizeof (unsigned int));

                   strncpy(numbers[nnum].name, ((char *) queryctrl.name) , MAXINDINAME);
                   strncpy(numbers[nnum].label, ((char *) queryctrl.name), MAXINDILABEL);
                   strncpy(numbers[nnum].format, "%0.f", MAXINDIFORMAT);
                   numbers[nnum].min    = queryctrl.minimum;
                   numbers[nnum].max    = queryctrl.maximum;
                   numbers[nnum].step   = queryctrl.step;
                   numbers[nnum].value  = queryctrl.default_value;
     
                   /* Get current value if possible */
                   CLEAR(control);
                   control.id = queryctrl.id;
                   if (0 == xioctl(fd, VIDIOC_G_CTRL, &control))
                            numbers[nnum].value = control.value;

                   /* Store ID info in INumber. This is the first time ever I make use of aux0!! */
                   num_ctrls[nnum] = queryctrl.id;

                   cerr << queryctrl.name << " -- min: " << queryctrl.minimum << " max: " << queryctrl.maximum << " step: " << queryctrl.step << " value: " << numbers[nnum].value << endl;

                   nnum++;
                  
                }
        } else if (errno != EINVAL) {
		if(numbers) free(numbers);
                return errno_exit ("VIDIOC_QUERYCTRL", errmsg);
        }

   }

  for (queryctrl.id = V4L2_CID_PRIVATE_BASE;  ;  queryctrl.id++)
  {
     if (0 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
        {
                if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
                {
                    cerr << queryctrl.name << " is disabled." << endl;
                    continue;
                }

                if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
                {
                   numbers = (numbers == NULL) ? (INumber *) malloc (sizeof(INumber)) :
                                                 (INumber *) realloc (numbers, (nnum+1) * sizeof (INumber));

                   num_ctrls = (num_ctrls == NULL) ? (unsigned int *) malloc  (sizeof (unsigned int)) :
                                                     (unsigned int *) realloc (num_ctrls, (nnum+1) * sizeof (unsigned int));

                   strncpy(numbers[nnum].name, ((char *) queryctrl.name) , MAXINDINAME);
                   strncpy(numbers[nnum].label, ((char *) queryctrl.name), MAXINDILABEL);
                   strncpy(numbers[nnum].format, "%0.f", MAXINDIFORMAT);
                   numbers[nnum].min    = queryctrl.minimum;
                   numbers[nnum].max    = queryctrl.maximum;
                   numbers[nnum].step   = queryctrl.step;
                   numbers[nnum].value  = queryctrl.default_value;
     
                   /* Get current value if possible */
                   CLEAR(control);
                   control.id = queryctrl.id;
                   if (0 == xioctl(fd, VIDIOC_G_CTRL, &control))
                            numbers[nnum].value = control.value;

                   /* Store ID info in INumber. This is the first time ever I make use of aux0!! */
                   num_ctrls[nnum] = queryctrl.id;
     
                   nnum++;
                  
                }
         }
         else break;
   } 

  /* Store numbers in aux0 */
  for (int i=0; i < nnum; i++)
      numbers[i].aux0 = &num_ctrls[i];

  nvp->np  = numbers;
  nvp->nnp = nnum;

  return nnum;

}

int  V4L2_Base::getControl(unsigned int ctrl_id, double *value,  char *errmsg)
{
   struct v4l2_control control;

   CLEAR(control);
   control.id = ctrl_id;

   if (-1 == xioctl(fd, VIDIOC_G_CTRL, &control))
     return errno_exit ("VIDIOC_G_CTRL", errmsg);
   *value = (double)control.value;
   return 0;
}

int  V4L2_Base::setINTControl(unsigned int ctrl_id, double new_value,  char *errmsg)
{
   struct v4l2_control control;

   CLEAR(control);

   //cerr << "The id is " << ctrl_id << " new value is " << new_value << endl;

   control.id = ctrl_id;
   control.value = (int) new_value;
   if (-1 == xioctl(fd, VIDIOC_S_CTRL, &control))
     return errno_exit ("VIDIOC_S_CTRL", errmsg);
   return 0;
}

int  V4L2_Base::setOPTControl(unsigned int ctrl_id, unsigned int new_value, char *errmsg)
{
   struct v4l2_control control;

   CLEAR(control);

   //cerr << "The id is " << ctrl_id << " new value is " << new_value << endl;

   control.id = ctrl_id;
   control.value = new_value;
   if (-1 == xioctl(fd, VIDIOC_S_CTRL, &control))
     return errno_exit ("VIDIOC_S_CTRL", errmsg);
   return 0;
}

bool V4L2_Base::enumerate_ext_ctrl (void)
{
  //struct v4l2_queryctrl queryctrl;
  
  CLEAR(queryctrl);
  
  queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  if (-1 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) return false;
  
  queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  while (0 == xioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
      {
	if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
	  cerr << "DISABLED--Control " << queryctrl.name << endl;
	  queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	  continue;
	}

	if (queryctrl.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
	  cerr << "Control Class " << queryctrl.name << endl;
	  queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	  continue;
	}
	
	cerr << "Control " << queryctrl.name << endl;
	
	if ((queryctrl.type == V4L2_CTRL_TYPE_MENU))//|| (queryctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU)) // not in < 3.5
	  enumerate_menu ();
	if (queryctrl.type == V4L2_CTRL_TYPE_BOOLEAN)
	  cerr << "  boolean" <<endl;
	if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
	  cerr << "  integer" <<endl;
	//if (queryctrl.type == V4L2_CTRL_TYPE_BITMASK) //not in < 3.1
	//  cerr << "  bitmask" <<endl;	
	if (queryctrl.type == V4L2_CTRL_TYPE_BUTTON)
	  cerr << "  button" <<endl;
	queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
      } 
    return true;
}
  
// TODO free opt/menu aux placeholders

bool  V4L2_Base::queryExtControls(INumberVectorProperty *nvp, unsigned int *nnumber, ISwitchVectorProperty **options, unsigned int *noptions, const char *dev, const char *group) {
  struct v4l2_control control;
  
  INumber *numbers = NULL;
  unsigned int *num_ctrls = NULL;
  int nnum=0;
  ISwitchVectorProperty *opt=NULL;
  unsigned int nopt=0;
  char optname[]="OPT000";
  char swonname[]="SET_OPT000";
  char swoffname[]="UNSET_OPT000";
  char menuname[]="MENU000";
  char menuoptname[]="MENU000_OPT000";
  *noptions = 0;
  *nnumber = 0;
  
  CLEAR(queryctrl);

  queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  if (-1 == ioctl (fd, VIDIOC_QUERYCTRL, &queryctrl)) return false;
  
  CLEAR(queryctrl);
  queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  while (0 == xioctl (fd, VIDIOC_QUERYCTRL, &queryctrl))
    {
      
      if (queryctrl.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
	cerr << "Control Class " << queryctrl.name << endl;
	queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	continue;
      }
      
      if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
	{
	  cerr << queryctrl.name << " is disabled." << endl;
	  queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	  continue;
	}
      
      
      if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER)
	{
	  numbers = (numbers == NULL) ? (INumber *) malloc (sizeof(INumber)) :
	    (INumber *) realloc (numbers, (nnum+1) * sizeof (INumber));
	  
	  num_ctrls = (num_ctrls == NULL) ? (unsigned int *) malloc  (sizeof (unsigned int)) :
	    (unsigned int *) realloc (num_ctrls, (nnum+1) * sizeof (unsigned int));
	  
	  strncpy(numbers[nnum].name, (const char *)entityXML((char *) queryctrl.name) , MAXINDINAME);
	  strncpy(numbers[nnum].label, (const char *)entityXML((char *) queryctrl.name), MAXINDILABEL);
	  strncpy(numbers[nnum].format, "%0.f", MAXINDIFORMAT);
	  numbers[nnum].min    = queryctrl.minimum;
	  numbers[nnum].max    = queryctrl.maximum;
	  numbers[nnum].step   = queryctrl.step;
	  numbers[nnum].value  = queryctrl.default_value;
	      
	  /* Get current value if possible */
	  CLEAR(control);
	  control.id = queryctrl.id;
	  if (0 == xioctl(fd, VIDIOC_G_CTRL, &control))
	    numbers[nnum].value = control.value;
	  
	  /* Store ID info in INumber. This is the first time ever I make use of aux0!! */
	  num_ctrls[nnum] = queryctrl.id;
	  
	  cerr << "Adding " << queryctrl.name << " -- min: " << queryctrl.minimum << " max: " << queryctrl.maximum << 
	    " step: " << queryctrl.step << " value: " << numbers[nnum].value << endl;
	  
	  nnum++;
          
	}
      if (queryctrl.type == V4L2_CTRL_TYPE_BOOLEAN)
	{
	  ISwitch *sw = (ISwitch *)malloc(2*sizeof(ISwitch));
	  snprintf(optname+3, 4, "%03d", nopt);
	  snprintf(swonname+7, 4, "%03d", nopt);
	  snprintf(swoffname+9, 4, "%03d", nopt);
	  
	  opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
	    (ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));
	  
	  CLEAR(control);
	  control.id = queryctrl.id;
	  xioctl(fd, VIDIOC_G_CTRL, &control);
	  
	  IUFillSwitch(sw, swonname, "Off", (control.value?ISS_OFF:ISS_ON));
	  sw->aux=NULL;
	  IUFillSwitch(sw+1, swoffname, "On", (control.value?ISS_ON:ISS_OFF));
	  (sw+1)->aux=NULL;
	  queryctrl.name[31]='\0';
	  IUFillSwitchVector (&opt[nopt], sw, 2, dev, optname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_1OFMANY, 0.0, IPS_IDLE);
	  opt[nopt].aux=malloc(sizeof(unsigned int));
	  *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);
	  
	  IDLog("Adding switch  %s (%s)\n", queryctrl.name, (control.value?"On":"Off"));
	  nopt += 1;
	}
      if (queryctrl.type == V4L2_CTRL_TYPE_BUTTON)
	{
	  ISwitch *sw = (ISwitch *)malloc(sizeof(ISwitch));
	  snprintf(optname+3, 4, "%03d", nopt);
	  snprintf(swonname+7, 4, "%03d", nopt);
	  //snprintf(swoffname+9, 4, "%03d", nopt);
	  
	  opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
	    (ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));

	  queryctrl.name[31]='\0';
	  IUFillSwitch(sw, swonname, (const char *)entityXML((char *)queryctrl.name), ISS_OFF);
	  sw->aux=NULL;
	  IUFillSwitchVector (&opt[nopt], sw, 1, dev, optname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_NOFMANY, 0.0, IPS_IDLE);
	  opt[nopt].aux=malloc(sizeof(unsigned int));
	  *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);	  
	  IDLog("Adding Button  \"%s\" \n", queryctrl.name);
	  nopt += 1;
	}
      if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
	{
	  ISwitch *sw = NULL;
	  unsigned int nmenuopt=0;
	  char sname[32];
	  snprintf(menuname+4, 4, "%03d", nopt);
	  snprintf(menuoptname+4, 4, "%03d", nopt);
	  menuoptname[7]='_';
	  
	  opt = (opt == NULL) ? (ISwitchVectorProperty *) malloc (sizeof(ISwitchVectorProperty)) :
	    (ISwitchVectorProperty *) realloc (opt, (nopt+1) * sizeof (ISwitchVectorProperty));
	  
	  CLEAR(control);
	  control.id = queryctrl.id;
	  xioctl(fd, VIDIOC_G_CTRL, &control);
	  
	  CLEAR(querymenu);
	  querymenu.id = queryctrl.id;
	  
	  for (querymenu.index = queryctrl.minimum;  querymenu.index <= queryctrl.maximum; querymenu.index++)
	    {
	      if (0 == xioctl (fd, VIDIOC_QUERYMENU, &querymenu))
		{
		  sw = (sw == NULL) ? (ISwitch *) malloc (sizeof(ISwitch)) :
		    (ISwitch *) realloc (sw, (nmenuopt+1) * sizeof (ISwitch));
		  snprintf(menuoptname+11, 4, "%03d", nmenuopt);
		  snprintf(sname, 31, "%s", querymenu.name);
		  sname[31]='\0';
		  IDLog("Adding menu item %s %s %s item %d index %d\n", querymenu.name, sname, menuoptname, nmenuopt, querymenu.index);
		  //IUFillSwitch(&sw[nmenuopt], menuoptname, (const char *)sname, (control.value==nmenuopt?ISS_ON:ISS_OFF));
		  IUFillSwitch(&sw[nmenuopt], menuoptname, (const char *)entityXML((char *)querymenu.name), (control.value==nmenuopt?ISS_ON:ISS_OFF));
		  sw[nmenuopt].aux=malloc(sizeof(unsigned int));
		  *(unsigned int *)(sw[nmenuopt].aux)=(querymenu.index);
		  nmenuopt+=1;
		} else
		{
		  //errno_exit("VIDIOC_QUERYMENU", errmsg);
		  //exit(3);
		}
	    }
	  
	  queryctrl.name[31]='\0';
	  IUFillSwitchVector (&opt[nopt], sw, nmenuopt, dev, menuname, (const char *)entityXML((char *)queryctrl.name), group, IP_RW, ISR_1OFMANY, 0.0, IPS_IDLE);
	  opt[nopt].aux=malloc(sizeof(unsigned int));
	  *(unsigned int *)(opt[nopt].aux)=(queryctrl.id);
	  
	  IDLog("Adding menu  %s (item %d set)\n", queryctrl.name, control.value);
	  nopt += 1;
	}

      //if (queryctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU)
      //	{
      //  IDLog("Control type not implemented\n");
      //}

	queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

  /* Store numbers in aux0 */
  for (int i=0; i < nnum; i++) 
    numbers[i].aux0 = &num_ctrls[i];
  
  nvp->np  = numbers;
  nvp->nnp = nnum;
  *nnumber=nnum;
  
  *options=opt;
  *noptions=nopt;

  return true;

}
