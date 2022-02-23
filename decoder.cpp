#include "jpg.h"
#include <iostream>
#include <fstream>


void readStartOfScan (std::ifstream& inFile, Header* const header) {
    std::cout << "Reading SOS marker\n";
    if(header->numComponents == 0) {
        std::cout << "Error - SOS detected before SOF\n";
        header->valid = false;
        return;
    }
    uint length = (inFile.get() << 8) + inFile.get();
    
    //reset flag
    for(uint i = 0; i < header->numComponents; ++i) {
        header->colorComponents[i].used = false;
    }

    byte numComponentsInScan = inFile.get();
    if(numComponentsInScan != header->numComponents) {
        std::cout << "Error - only baseline JPEGs supported\n";
        header->valid = false;
        return;
    }

    for(uint i = 0; i < numComponentsInScan; ++i) {
        byte componentID = inFile.get();
        if(header->zeroIndex) {
            componentID += 1;
        }
        if (componentID > header->numComponents) {
            std::cout << "Error - invalid component ID\n";
            header->valid = false;
            return;
        }
        
        ColorComponent* cmpnt = &header->colorComponents[componentID-1];

        if(cmpnt->used) {
            std::cout << "Error - Duplicate color component\n";
            header->valid = false;
            return;
        }
        cmpnt->used = true;

        byte huffmanTableInfo = inFile.get();
        cmpnt->dcHuffmanTableID = huffmanTableInfo >> 4;
        cmpnt->acHuffmanTableID = huffmanTableInfo & 0x0F;

        if(cmpnt->dcHuffmanTableID > 3) {
            std::cout << "Invalid DC Huffman Table ID: " << (uint)cmpnt->dcHuffmanTableID <<"\n";
            header->valid = false;
            return;
        }
        if(cmpnt->acHuffmanTableID > 3) {
            std::cout << "Invalid AC Huffman Table ID: "<< (uint)cmpnt->acHuffmanTableID <<"\n";
            header->valid = false;
            return;
        }
    }
    header->startOfSelection = inFile.get();
    header->endOfSelection = inFile.get();
    byte successiveApproximation = inFile.get();
    header->successiveApproxHigh = successiveApproximation >> 4;
    header->successiveApproxLow = successiveApproximation & 0x0F;

    //Verify baseline JPEG, they dont use spectral selection or successive approximation
    if (header->startOfSelection != 0 || header->endOfSelection != 63) {
        std::cout << "Error - invalid spectral selection\n";
        header->valid = false;
        return;
    }
    if (header->successiveApproxHigh !=0 || header->successiveApproxLow !=0) {
        std::cout << "Error - invalid successive approximation value\n";
        header->valid = false;
        return;
    }
    if(length - 6 - (2*numComponentsInScan) != 0) {
        std::cout << "Error - invalid SOS marker\n";
        header->valid = false;
        return;
    }
}


//only supporting SOF0 at this time
//SOF tells frame type, dimensions, and number of color components
void readStartOfFrame (std::ifstream& inFile, Header* const header) {
    std::cout << "Reading SOF marker\n";
        if(header->numComponents != 0) {
        std::cout << "Error - multiple SOFs\n";
        header->valid = false;
        return;
    }
    
    uint length = (inFile.get() << 8) + inFile.get();
    byte precision = inFile.get();
    
    //precision has to be 8 only
    if(precision != 8) {
        std::cout << "Error - invalid precision\n" << (uint)precision << "\n";
        header->valid = false;
        return;
    }

    header->height = (inFile.get() << 8) + inFile.get();
    header->width = (inFile.get() << 8) + inFile.get();
    if (header->height == 0 || header->width == 0) {
        std::cout << "Error - invalid dimensions\n";
        header->valid = false;
        return;
    }
    
    header->numComponents = inFile.get();
    if (header->numComponents == 4) {
        std::cout << "Error - CMYK unsupported\n";
        header->valid = false;
        return;
    }

    //read component data
    for(uint i=0; i < header->numComponents; i++) {
        byte componentID = inFile.get();
        
        //componentID is usually 1,2,3 and not 0,1,2 but if we see ID start from 0, force them to start from 1 for consistency
        if (componentID == 0) {
            header->zeroIndex = true;
        }
        if (header->zeroIndex) {
            componentID += 1;
        }


        if (componentID == 4 || componentID == 5) {
            std::cout << "Error - YIQ unsupported\n";
            header->valid = false;
            return;
        }
        if (componentID == 0 || componentID > 3) {
            std::cout << "Error - invalid component\n";
            header->valid = false;
            return;
        }
        
        ColorComponent* component = &header->colorComponents[componentID - 1];
        if (component->used) {
            std::cout << "Error - duplicate color component detected\n";
            header->valid = false;
            return;
        }
        component->used = true;
        byte samplingFactor = inFile.get();
        component->horizontalSamplingFactor = samplingFactor >> 4;
        component->verticalSamplingFactor = samplingFactor & 0x0F;
        
        //place holder for now, to support SF = 2 later
        if (component->horizontalSamplingFactor != 1 || component->verticalSamplingFactor !=1) {
            std::cout << "Error - sampling factor not supported\n";
            header->valid = false;
            return;
        }

        component->quantizationTableID = inFile.get();
        if (component->quantizationTableID > 3) {
            std::cout << "Error - invalid quantization table ID in components\n";
            header->valid = false;
            return;
        }
    }
    //if length of bytes read does not line up
    if (length - 8 - (3 * header->numComponents) != 0) {
        std::cout << "Error invalid SOF marker\n";
        header->valid = false;
        return;
    }
}


//can contain more than one huffman table
void readHuffmanTable (std::ifstream& inFile, Header* const header) {
    std::cout << "Reading Huffman Tables\n";
    int length = (inFile.get() << 8) + inFile.get();
    length -= 2;

    while (length > 0) {
        byte tableInfo = inFile.get();
        byte tableID = tableInfo & 0x0F;
        bool acTable = tableInfo >> 4;
        if(tableID > 3) {
            std::cout << "Error - invalid huffman table ID" << (uint)tableID <<"\n";
            header->valid = false;
            return;
        }

        //AC huffman table is used for values at index 0,0 of 8,8 MCU, rest use DC table
        HuffmanTable* hTable;
        if(acTable) {
            hTable = &header->acHuffmanTables[tableID];
        }
        else {
            hTable = &header->dcHuffmanTables[tableID];
        }
        hTable->set=true;

        hTable->offsets[0] = 0;
        uint allSymbols = 0;
        //count all symbols and create offsets for when the next symbol with different length starts
        for(uint i = 1; i <= 16; ++i) {
            allSymbols += inFile.get();
            hTable->offsets[i] = allSymbols;
        }
        if (allSymbols > 162) {
            std::cout << "Error - too many symbols in HT\n";
            header->valid = false;
            return;
        }
        //store symbols
        for(uint i = 0; i < allSymbols; ++i) {
            hTable->symbols[i] = inFile.get();
        }

        length -= 17 + allSymbols;
    }
    if (length != 0) {
        std::cout << "Error - invalid DHT marker\n";
        header->valid = false;
        return;
    }
}


//DQT can contain more than one quantization table
void readQuantizationTable (std::ifstream& inFile, Header* const header) {
    std::cout << "Reading Quantization tables\n";
    int length = (inFile.get() << 8) + inFile.get();
    length -= 2;

    while (length > 0) {
        byte tableInfo = inFile.get();
        length -= 1;
        //lower nibble contains table ID
        byte tableID = tableInfo & 0x0F;
        
        if(tableID > 3) {
            std::cout << "Error - invalid Quantization table ID "<< (uint)tableID <<"\n";
            header->valid = false;
            return;
        }
        
        header->quantizationTables[tableID].set = true;

        //check if it is 16-bit quantization table and read it
        if (tableInfo >> 4 != 0) {
            for(uint i = 0; i < 64; ++i) {
                header->quantizationTables[tableID].table[zigzagMap[i]] = (inFile.get() << 8) + inFile.get();
            }
            length -= 128;
        }
        //8-bit quantization table
        else {
            for (uint i = 0; i < 64; ++i) {
                header->quantizationTables[tableID].table[zigzagMap[i]] = inFile.get();
            }
            length -=64;
        }
    }
    //if length is -ve due to subtractions in length
    if (length != 0) {
        std::cout << "Error - invalid DQT marker\n";
        header->valid = false;
    }
    
}


void readRestartInterval(std::ifstream& inFile, Header* const header) {
    std::cout << "Reading DRI marker\n";
    uint length = (inFile.get() << 8) + inFile.get();
    
    header->restartInterval = (inFile.get() << 8) + inFile.get();
    if(length - 4 != 0) {
        std::cout << "Error - invalid DRI marker\n";
        header->valid = false;
    }
}


void readAPPN (std::ifstream& inFile, Header* const header) {
    std::cout << "Reading APPN marker\n";
    //next two bytes after any marker contains the length
    uint length = (inFile.get() << 8) + inFile.get();
    for (uint i = 0; i < length-2 ; ++i) {
        inFile.get();
    }
}


void readComments (std::ifstream& inFile, Header* const header) {
    std::cout << "Reading COM marker\n";
    //next two bytes after any marker contains the length
    uint length = (inFile.get() << 8) + inFile.get();
    for (uint i = 0; i < length-2 ; ++i) {
        inFile.get();
    }
}


Header* readJPG (const std::string& filename) {
    //open file
    std::ifstream inFile = std::ifstream(filename, std::ios::in | std::ios::binary);
    if (!inFile.is_open()){
        std::cout << "Error opening file!\n";
        return nullptr;
    }

    Header* header = new(std::nothrow) Header;
    
    if (header == nullptr) {
        std::cout << "Memory error!\n";
        inFile.close();
        return nullptr;
    }

    byte last = inFile.get();
    byte current = inFile.get();

    //jpeg images start with FF D8 (start of image)
    if (last != 0xFF || current != SOI) {
        std::cout<<"Invalid file\n";
        header->valid = false;
        inFile.close();
        return header;
    }

    last = inFile.get();
    current = inFile.get();

    //read markers
    while (header->valid) {

        //check if program reaches the end without detecting eof marker
        
        if (!inFile){
            std::cout << "Error - file ended prematurely\n";
            header->valid = false;
            inFile.close();
            return header;
        }

        if (last != 0xFF) {
            std::cout << "Error - marker expected\n";
            header->valid = false;
            inFile.close();
            return header;
        }

        if (current == SOF0) {
            header->frameType = SOF0;
            readStartOfFrame(inFile, header);
        }
        else if (current == DRI) {
            readRestartInterval(inFile, header);
        }
        else if (current == DQT) {
            readQuantizationTable(inFile, header);
        }
        else if (current == DHT) {
            readHuffmanTable(inFile, header);
        }
        else if (current == SOS) {
            readStartOfScan(inFile, header);
            break; //delete this
        }
        else if (current >= APP0 && current <= APP15) {
            readAPPN(inFile, header);
        }
        else if (current == COM) {
            readComments(inFile, header);
        }
        else if (current == TEM) {
            //TEM is useless empty marker, read the next byte(marker)
        }
        //useless skippable markers
        else if((current >= JPG0 && current <= JPG13) || current == DNL || current == DHP || current == EXP) {
            readComments(inFile, header);
        }
        //continous 0x0f are valid, move to next byte
        else if (current == 0x0F) {
            current = inFile.get();
            continue;
        }

        else if(current == SOI) {
            std::cout << "Error - embedded jpeg not supported\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if(current == EOI) {
            std::cout << "Error - EOI before SOS\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if(current == DAC) {
            std::cout << "Error - Arithmetic coding not supported\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (current >= SOF0 && current <=SOF15) {
            std::cout << "Error - unsupported SOF\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (current >= RST0 && current <= RST7) {
            std::cout << "Error - RSTN before SOS\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else {
            std::cout << "Error - unknown marker : 0x " << std::hex << current << std::dec << "\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        
        last = inFile.get();
        current = inFile.get();
    }

    //read huffman data after SOS
    if (header->valid) {
        current = inFile.get();
        //break manually from the loop on detecting EOI or some error
        while(true) {
            if (!inFile) {
                std::cout << "Error - file ended prematurely\n";
                header->valid = false;
                inFile.close();
                return header;
            }
            //look at two bytes to identify markers
            last = current;
            current = inFile.get();
            if (last == 0xFF) {
                //check for end of image marker
                if (current == EOI) {
                    break;
                }
                else if (current == 0x00) {
                    //store 0xFF in huffman data and ignore 0x00
                    header->huffmanData.push_back(last);
                    current = inFile.get();
                }
                else if (current == 0xFF) {
                    //ignore multiple 0xFF
                    continue;
                }
                else if (current >= RST0 && current <= RST7) {
                    //ignore restart markers and read the next byte
                    current = inFile.get();
                }
                else {
                    std::cout << "Error - invalid marker while reading huffman data 0x"<<std::hex<<(uint)current<<std::dec<<"\n";
                    header->valid=false;
                    inFile.close();
                    return header;
                }
            }
            else {
                header->huffmanData.push_back(last);
            }
        }
    }
    else {
        return header; // placeholder 2 maybe
    }

    //verify header info
    if(header->numComponents != 1 && header->numComponents != 3) {
        std::cout << "Error - number of color components need to be 1 or 3";
        header->valid=false;
        inFile.close();
        return header;
    }

    for(uint i = 0; i < header->numComponents ; ++i) {
        if (header->quantizationTables[header->colorComponents[i].quantizationTableID].set == false) {
            std::cout << "Error - Color component using uninitialized quantization table\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (header->dcHuffmanTables[header->colorComponents[i].dcHuffmanTableID].set == false) {
            std::cout << "Error - Color component using uninitialized DC huffman table\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (header->acHuffmanTables[header->colorComponents[i].acHuffmanTableID].set == false) {
            std::cout << "Error - Color component using uninitialized AC huffman table\n";
            header->valid = false;
            inFile.close();
            return header;
        }
    }

    inFile.close();
    return header;
}


void printHeader (const Header* const header) {
    if (header == nullptr) {
        return;
    }
    std::cout << "----------DQT----------\n";
    for (uint i = 0; i < 4; ++i) {
        if (header->quantizationTables[i].set) {
            std::cout << "Table ID: " << i <<"\n";
            std::cout << "Table data:";
            for (uint j = 0; j < 64; ++j) {
                if (j % 8 == 0) {
                    std::cout << "\n";
                }
                std::cout << header->quantizationTables[i].table[j] << ' ';
            }
            std::cout << "\n";
        }
    }
    std::cout << "----------SOF----------\n";
    std::cout << "Frame type = 0x" << std::hex << (uint)header->frameType << std::dec << "\n";
    std::cout << "Height = " << header->height << "\n";
    std::cout << "Width = " << header->width << "\n";
    for(uint i = 0; i < header->numComponents; i++) {
        std::cout << "Component ID = " << i+1 <<"\n";
        std::cout << "Horizontal Sampling Factor = " << (uint)header->colorComponents[i].horizontalSamplingFactor << "\n";
        std::cout << "Vertical Sampling Factor = " << (uint)header->colorComponents[i].verticalSamplingFactor << "\n";
        std::cout << "Quantization Table used (ID) = " << (uint)header->colorComponents[i].quantizationTableID << "\n";
    }
    std::cout << "\n----------DRI----------\n";
    std::cout << "Restart interval = " << header->restartInterval << "\n";
    std::cout << "\n----------Huffman Tables---------- \n";
    std::cout << "DC Tables :\n";
    for(uint i = 0; i < 4; ++i) {
        if (header->dcHuffmanTables[i].set) {
            std::cout << "Table ID: "<<i<<std::endl;
            std::cout << "Symbols :\n";
            for (uint j = 0; j < 16; ++j) {
                std::cout<< (j+1) <<": ";  //(j+1) = code length
                for(uint k = header->dcHuffmanTables[i].offsets[j]; k < header->dcHuffmanTables[i].offsets[j+1]; ++k) {
                    std::cout<< std::hex << (uint)header->dcHuffmanTables[i].symbols[k] << std::dec << " ";
                }
                std::cout<<std::endl;
            }
        }
    }

    std::cout << "AC Tables :\n";
    for(uint i = 0; i < 4; ++i) {
        if (header->acHuffmanTables[i].set) {
            std::cout << "Table ID: "<<i<<std::endl;
            std::cout << "Symbols :\n";
            for (uint j = 0; j < 16; ++j) {
                std::cout<< (j+1) <<": ";  //(j+1) = code length
                for(uint k = header->acHuffmanTables[i].offsets[j]; k < header->acHuffmanTables[i].offsets[j+1]; ++k) {
                    std::cout<< std::hex << (uint)header->acHuffmanTables[i].symbols[k] << std::dec << " ";
                }
                std::cout<<std::endl;
            }
        }
    }
    std::cout << "\n----------SOS----------\n";
    std::cout << "Start of selection : " << (uint)header->startOfSelection << "\n";
    std::cout << "End of selection : " << (uint)header->endOfSelection << "\n";
    std::cout << "successive approximation high : "<< (uint)header->successiveApproxHigh << "\n";
    std::cout << "successive approximation low : " << (uint)header->successiveApproxLow << "\n";
    std::cout << "Color components :\n";
    for(uint i=0; i<header->numComponents; ++i) {
        std::cout << "Component ID : " <<(i+1)<<"\n";
        std::cout << "DC Huffman Table ID: "<<(uint)header->colorComponents[i].dcHuffmanTableID<<"\n";
        std::cout << "AC Huffman Table ID: "<<(uint)header->colorComponents[i].acHuffmanTableID<<"\n";
    }
    std::cout << "Length of huffman data : " << header->huffmanData.size() << "\n";

}

//output huffman codes from symbols in huffman table stored in 1-D array
void getCodes(HuffmanTable& const htable) {
    uint code = 0;

    //for all code lengths (0-15)
    for(uint i=0; i<16; ++i) {
        
        //for code length=i+1 iterate till index of next code length by using offset
        //the offset array provides index for code lengths(stored in symbols in that order)
        //at i=0, the offset array gives starting index of code lengths = 1, i.e (i+1)
        for(uint j = htable.offsets[i]; j < htable.offsets[i+1]; ++j) {
            
            //finalise current code
            htable.codes[j] = code;
            //add 1 to code candidate
            code += 1;
        }

        //append 0 to right of code candidate
        code <<= 1;
    }
}

//helper class to read bits from byte vector
class BitReader {
    private:
        uint nextBit = 0;
        uint nextByte = 0;
        const std::vector<byte>& data;
    public:
        BitReader(const std::vector<byte>& d) : data(d) {}

        //read one bit or return -1 if all bits have been read
        int readBit() {
            if(nextByte >= data.size()) {
                return -1;
            }
            int bit = (data[nextByte] >> (7-nextBit)) & 1;
            nextBit += 1;
            if(nextBit == 8) {
                nextBit = 0;
                nextByte += 1;
            }
            return bit;
        }

        int readMultipleBits(const uint length) {
            int bits = 0;
            for (uint i=0; i < length; ++i) {
                int bit = readBit();
                if (bit == -1) {
                    bits = -1;
                    break;
                }
                bits = (bits << 1) | bit;
            }
            return bits;
        }
};


//fill coefficients of an MCU component based on Huffman Codes
//read from bit-reader
bool decodeMCUComponent(BitReader &b, int* const component, const HuffmanTable& dcTable, const HuffmanTable& acTable) {
    //get DC values for this mcu component
    byte length = getNextSymbol(b, dcTable);
}


MCU* decodeHuffmanData(Header* const header){
    const uint mcuHeight = (header->height + 7)/8;
    const uint mcuWidth = (header->width +7)/8;
    MCU* mcus = new (std::nothrow) MCU[mcuHeight * mcuWidth];
    if(mcus == nullptr) {
        std::cout << "Error - memory error\n";
        return nullptr;
    }

    for(uint i = 0; i < 4; ++i) {
        if(header->dcHuffmanTables[i].set) {
            getCodes(header->dcHuffmanTables[i]);
        }
        if(header->acHuffmanTables[i].set) {
            getCodes(header->acHuffmanTables[i]);
        }
    }
    BitReader reader(header->huffmanData);

    for(uint i=0; i< mcuHeight*mcuWidth; ++i) {
        for(uint j=0; j < header->numComponents; ++j) {
            if(!decodeMCUComponent(reader, 
                                    mcus[i][j], 
                                    header->dcHuffmanTables[header->colorComponents[j].dcHuffmanTableID], 
                                    header->acHuffmanTables[header->colorComponents[j].acHuffmanTableID])) {
                delete[] mcus;
                return nullptr;
            }
        }
    }

    return mcus;
}


//helper functions to write 4-byte int and 2-byte short in little endian
void writeInt (std::ofstream& outFile, const uint s) {
    outFile.put((s >> 0) & 0xFF);
    outFile.put((s >> 8) & 0xFF);
    outFile.put((s >> 16) & 0xFF);
    outFile.put((s >> 24) & 0xFF);
}

void writeShort (std::ofstream& outFile, const uint s) {
    outFile.put((s >> 0) & 0xFF);
    outFile.put((s >> 8) & 0xFF);
}


//output Bitmap image
void writeBMP(const Header* const header, const MCU* const mcus, const std::string& outFilename) {
    std::ofstream outFile = std::ofstream(outFilename, std::ios::out | std::ios::binary);
    if(!outFile.is_open()) {
        std::cout << "Error opening output file\n";
        return;
    }

    
    const uint mcuHeight = (header->height + 7)/8;
    const uint mcuWidth = (header->width + 7)/8;
    const uint paddingSize = (header->width) % 4;
    const uint totalSize = 14 + 12 + header->height * header->width * 3 + paddingSize * header->height;

    outFile.put('B');
    outFile.put('M');
    writeInt(outFile, totalSize);
    writeInt(outFile, 0);
    writeInt(outFile, 0x1A);
    writeInt(outFile, 12);
    writeShort(outFile, header->width);
    writeShort(outFile, header->height);
    writeShort(outFile, 1);
    writeShort(outFile, 24);

    for(uint y = header->height - 1; y < header->height; --y) {
        const uint mcuRow = y / 8;
        const uint pixelRow = y % 8;
        for(uint x = 0; x < header->width; ++x) {
            const uint mcuColumn = x / 8;
            const uint pixelColumn = x % 8;
            const uint mcuIndex = mcuRow * mcuWidth + mcuColumn;
            const uint pixelIndex = pixelRow * 8 + pixelColumn;
            outFile.put(mcus[mcuIndex].b[pixelIndex]);
            outFile.put(mcus[mcuIndex].g[pixelIndex]);
            outFile.put(mcus[mcuIndex].r[pixelIndex]);
        }
        for (uint i = 0; i < paddingSize; ++i) {
            outFile.put(0);
        }
    }

    outFile.close();
}


int main (int argc, char** argv){
    std::cout << "program running!\n";
    if (argc < 2) {
        std::cout<<"Error! invalid arguments\n";
        return 1;
    }
    for (uint i=1; i < argc; ++i) {
        const std::string filename(argv[i]);
        std::cout<<"Filename = "<<filename<<"\n";
        Header* header = readJPG(filename);

        if (header == nullptr) {
            continue;
        }
        if (header->valid == false) {
            delete header;
            continue;
        }

        printHeader(header);
        
        //huffman coded bitstream
        MCU* mcus = decodeHuffmanData(header);
        if(mcus == nullptr) {
            delete header;
            continue;
        }

        //write bmp file
        const std::size_t pos = filename.find_last_of('.');
        const std::string outFileName = (pos == std::string::npos) ? (filename + ".bmp") : (filename.substr(0 , pos) + ".bmp");
        writeBMP(header, mcus, outFileName);

        delete[] mcus;
        delete header;
    }
    return 0;
}