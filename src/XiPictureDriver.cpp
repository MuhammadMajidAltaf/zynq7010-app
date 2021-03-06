#include "XiPictureDriver.h"

#include "XiLog.h"

namespace Xilinx{

XiPictureDriver::XiPictureDriver()
{
    LOG(INFO_LEVEL, "start ..构造");
    mPicture = new Tpicture;
    memset(mPicture, 0, sizeof(Tpicture));

    mPicture->PhysLength = 0x400000*4*2;
    mPicture->PhysAddress = 0x1E000000;

    mPicture->Handler = open("/dev/Xipicture", O_RDWR|O_SYNC);
    if(mPicture->Handler < 0)
    {
        LOG(ERROR_LEVEL, "open Xipicture dev fail");
    }
    else{
        mPicture->VirtualAddress = mmap(NULL, mPicture->PhysLength,
            PROT_READ|PROT_WRITE,
            MAP_SHARED,
            mPicture->Handler , 0);
        if(mPicture->VirtualAddress != NULL)
        {
           mPicture->Fb1VirtualAddress = (char*)mPicture->VirtualAddress + 0x000000;
           mPicture->Fb2VirtualAddress = (char*)mPicture->VirtualAddress + 0x400000;
           mPicture->Fb3VirtualAddress = (char*)mPicture->VirtualAddress + 0x800000;
           mPicture->Fb4VirtualAddress = (char*)mPicture->VirtualAddress + 0xC00000;

           //分配0x1F00,0000 ~ 0x2000,0000内存块
           int loop;
           for(loop = 0; loop < 4; loop++)
           {
               DDrDataBuff[loop].Index    = loop+1;
               DDrDataBuff[loop].Size     = 0x400000;
               DDrDataBuff[loop].Pointer  = (char*)mPicture->VirtualAddress+0x1000000+0x400000*loop;
               DDrDataBuff[loop].isValid  = true;
           }

        }else{
            LOG(ERROR_LEVEL, "Xipicture mmap!");
        }
    }

    mPicture->AxiPhysLength = 0x20000;
    mPicture->AxiPhysAddress = 0x40600000;
    mPicture->AxiHandler = open("/dev/XiAxi", O_RDWR|O_SYNC);
    if(mPicture->AxiHandler < 0)
    {
        LOG(ERROR_LEVEL, "open XiAxi dev fail");
    }
    else{
        mPicture->AxiVirtualAddress = mmap(NULL, mPicture->AxiPhysLength,
                PROT_READ|PROT_WRITE,
                MAP_SHARED,
                mPicture->AxiHandler , 0);
        if(mPicture->AxiVirtualAddress != NULL)
        {
            mPicture->AxiInt = (volatile unsigned int*)mPicture->AxiVirtualAddress;
        }else{
            LOG(ERROR_LEVEL, "Axi mmap!");
        }
    }

    //创建线程
    workfun = NULL;
    mthreadWork = true;
    mthread = new std::thread(ImageThread, this);
    LOG(INFO_LEVEL, "end   ..构造");
}

XiPictureDriver::~XiPictureDriver()
{
    LOG(INFO_LEVEL, "start ..虚构");
    mthreadWork = false;
    mthread->join();
    delete mthread;
    mthread = NULL;

    if(mPicture != NULL)
    {
        if(mPicture->AxiVirtualAddress != NULL)
        {
            munmap(mPicture->AxiVirtualAddress, mPicture->AxiPhysLength);
            mPicture->AxiVirtualAddress = NULL;
        }

        if(mPicture->AxiHandler >= 0)
            close(mPicture->AxiHandler);


        if(mPicture->VirtualAddress != NULL)
        {
            munmap(mPicture->VirtualAddress, mPicture->PhysLength);
            mPicture->VirtualAddress = NULL;
        }

        if(mPicture->Handler >= 0)
            close(mPicture->Handler);

        delete mPicture;
        mPicture = NULL;
    }
    LOG(INFO_LEVEL, "end   ..虚构");
}

 void* XiPictureDriver::getPictureBuff(int Number)
 {
     if(mPicture->VirtualAddress != NULL)
     {
         switch(Number)
         {
             case 0:
                 return (void*)mPicture->Fb1VirtualAddress;
                 break;
             case 1:
                 return (void*)mPicture->Fb2VirtualAddress;
                 break;
             case 2:
                 return (void*)mPicture->Fb3VirtualAddress;
                 break;
             case 3:
                 return (void*)mPicture->Fb4VirtualAddress;
                 break;
             default:
                 return NULL;
                 break;
         }
     }else{
         return NULL;
     }
 }

int XiPictureDriver::getPictureBuff(int Number, char** buff)
{//根据Number ，获取buff地址
    if(mPicture->VirtualAddress != NULL)
    {
        switch(Number)
        {
            case 0:
                *buff = mPicture->Fb1VirtualAddress;
                break;
            case 1:
                *buff = mPicture->Fb2VirtualAddress;
                break;
            case 2:
                *buff = mPicture->Fb3VirtualAddress;
                break;
            case 3:
                *buff = mPicture->Fb4VirtualAddress;
                break;
            default:
                *buff = NULL;
                return -1;
        }
        return 0;
    }else{
        return -2;
    }
}

 void   XiPictureDriver::clearPictureBuff(int Number) //清理状态
{


}

void XiPictureDriver::softTrigger()
{
    if(mPicture->AxiHandler < 0)
        return;
    *(mPicture->AxiInt + 0xE4/4) = 1;
}

int XiPictureDriver::getWidth()
{
    if(mPicture->AxiHandler < 0)
        return -1;
    return *(mPicture->AxiInt + 0x80/4);
}


int XiPictureDriver::getHeight()
{
    if(mPicture->AxiHandler < 0)
        return -1;
    return *(mPicture->AxiInt + 0x90/4);
}

uint8_t XiPictureDriver::getReadBuffNo()
{
    uint32_t rel = *((volatile unsigned int*) (mPicture->AxiInt + 0xD4/4));
    uint8_t  buffNo = ((rel>>4) & 0x3);
    uint8_t  buffstatus = (rel & 0xF);
  //  printf("#### %x  %x\n", buffNo, buffstatus);
    while( mthreadWork && !buffstatus)
    {
        usleep(500);
        rel = *((volatile unsigned int*) (mPicture->AxiInt + 0xD4/4));
        buffNo = ((rel>>4) & 0x3);
        buffstatus = (rel & 0xF);
    }
  //  printf("2*****  %x  %x\n", buffNo, buffstatus);
    return buffNo;
}


void XiPictureDriver::lockBuff(uint8_t& BuffNo)
{
    uint32_t status = *(mPicture->AxiInt+0xD8/4);
    *(mPicture->AxiInt+0xD8/4) = status|(0x10<<BuffNo);
}

void XiPictureDriver::unlockBuff(uint8_t& BuffNo)
{
    uint32_t status = *(mPicture->AxiInt+0xD8/4);
    *(mPicture->AxiInt+0xD8/4) = status|(0x1<<BuffNo);
}

uint32_t XiPictureDriver::getImageBuff(unsigned char** buff)
{
    uint8_t buffNo = getReadBuffNo();
    lockBuff(buffNo);
    getPictureBuff(buffNo, (char**)buff);
    unlockBuff(buffNo);
    return 0;
}

uint32_t XiPictureDriver::getImage(unsigned char** buff)
{
    getImageBuff(buff);
    return (getWidth()*getHeight());
}


void XiPictureDriver::registerImageCallback(FHandler* pfun)
{
    this->mMtx.lock();
    this->workfun = pfun;
    this->mMtx.unlock();
}


void XiPictureDriver::ImageThread(XiPictureDriver* PictureDriver)
{
    while(PictureDriver->mthreadWork)
    {
        PictureDriver->mMtx.lock();
        if(PictureDriver->workfun !=NULL)
        {
            TImageType* info = new TImageType;
            PictureDriver->getImageBuff( &(info->imagebuff));
            uint32_t width = PictureDriver->getWidth();
            uint32_t height = PictureDriver->getHeight();
            info->width = width;
            info->height = height;
            info->imagelen = width * height;
            if((PictureDriver->workfun !=NULL) && PictureDriver->mthreadWork)
                PictureDriver->workfun(info);
            delete info;
        }
        PictureDriver->mMtx.unlock();
        usleep(1000);
    }
}



void XiPictureDriver::setRegisterValue(uint32_t offset, uint32_t value)
{
    *(mPicture->AxiInt+offset/4) = value;
}

uint32_t XiPictureDriver::getRegisterValue(uint32_t offset)
{
    return *(mPicture->AxiInt+offset/4);
}


char* XiPictureDriver::AllocDataBuff(uint8_t index)
{
    if(index >=4)
        return NULL;

    DDrDataBuff[index].isValid = false;
    return DDrDataBuff[index].Pointer;
}

void XiPictureDriver::FreeDataBuff(uint8_t index)
{
    if(index >=4)
        return;
    DDrDataBuff[index].isValid = true;

}

void*   XiPictureDriver::PhyaddrToVirtualaddr(uint32_t Phyaddr)
{
   return (void*)( ((char*)mPicture->VirtualAddress) + (Phyaddr - mPicture->PhysAddress));
}

uint32_t XiPictureDriver::VirtualaddrToPhyaddr(void* Virtualaddr)
{
    return (mPicture->PhysAddress + ((char*)Virtualaddr - (char*)mPicture->VirtualAddress));
}


}

