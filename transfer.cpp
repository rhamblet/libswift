/*
 *  transfer.cpp
 *  some transfer-scope code
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include <errno.h>
#include <string>
#include <sstream>

#include "ext/seq_picker.cpp" // FIXME FIXME FIXME FIXME
#include "ext/vod_picker.cpp"
#include "ext/rf_picker.cpp"

using namespace swift;

#define BINHASHSIZE (sizeof(bin64_t)+sizeof(Sha1Hash))

// FIXME: separate Bootstrap() and Download(), then Size(), Progress(), SeqProgress()

FileTransfer::FileTransfer(int td, std::string filename, const Sha1Hash& root_hash, bool force_check_diskvshash,
                           popt_cont_int_prot_t cipm, uint32_t chunk_size, bool zerostate, std::string metadir) :
    ContentTransfer(FILE_TRANSFER), availability_(NULL), zerostate_(zerostate)
{
    td_ = td;

    Handshake hs;
    hs.cont_int_prot_ = cipm;
    SetDefaultHandshake(hs);

    std::string destdir;
    std::string metaprefix;
    int ret = file_exists_utf8(filename);
    if (ret == 2 && root_hash != Sha1Hash::ZERO) {
        // Filename is a directory, download root_hash there
        destdir = filename;
        filename = destdir+FILE_SEP+root_hash.hex();
        if (metadir == "")
            metaprefix = filename;
        else
            metaprefix = metadir+root_hash.hex();
    } else {
        destdir = dirname_utf8(filename);
        if (destdir == "") {
            destdir = ".";
            if (metadir ==  "")
                metaprefix = filename;
            else
                metaprefix = metadir+filename;
        } else {
            // Filename with directory
            std::string basename = basename_utf8(filename);
            if (metadir == "")
                metaprefix = filename;
            else
                metaprefix = metadir+basename;
        }
    }

    std::string hash_filename;
    hash_filename.assign(metaprefix);
    hash_filename.append(".mhash");

    std::string binmap_filename;
    binmap_filename.assign(metaprefix);
    binmap_filename.append(".mbinmap");

    // METADIR MULTIFILE
    std::string meta_mfspec_filename;
    meta_mfspec_filename.assign(metaprefix);
    meta_mfspec_filename.append(".mfspec");

    // MULTIFILE
    storage_ = new Storage(filename,destdir,td_,0,meta_mfspec_filename);
    if (!storage_->IsOperational()) {
        fprintf(stderr, "[WARN] [1]\n");
        delete storage_;
        fprintf(stderr, "[WARN] [2]\n");
        storage_ = NULL;
        SetBroken();
        return;
    }

    // Arno, 2013-02-25: Create HashTree even when PROT_NONE to enable
    // automatic size determination via peak hashes.
    if (!zerostate_) {
        hashtree_ = (HashTree *)new MmapHashTree(storage_,root_hash,chunk_size,hash_filename,force_check_diskvshash,
                    binmap_filename);
        availability_ = new Availability(SWIFT_MAX_OUTGOING_CONNECTIONS);

        if (ENABLE_VOD_PIECEPICKER)
            picker_ = new VodPiecePicker(this);
        else
            //picker_ = new SeqPiecePicker(this);
            picker_ = new RFPiecePicker(this);
        picker_->Randomize(rand()&63);
    } else {
        // ZEROHASH
        hashtree_ = (HashTree *)new ZeroHashTree(storage_,root_hash,chunk_size,hash_filename,binmap_filename);
    }

    UpdateOperational();
}


void FileTransfer::UpdateOperational()
{
    if (!hashtree_->IsOperational() || !storage_->IsOperational())
        SetBroken();

    if (zerostate_ && !hashtree_->is_complete())
        SetBroken();
}


FileTransfer::~FileTransfer()
{
    if (hashtree_ != NULL) {
        delete hashtree_;
        hashtree_ = NULL;
    }

    if (!IsZeroState()) {
        if (picker_ != NULL) {
            delete picker_;
            picker_ = NULL;
        }
        if (availability_ != NULL) {
            delete availability_;
            // ~ContentTransfer calls CloseChannels which calls Channel::Close which tries to unregister
            // the availability of that peer from availability_, which has been deallocated here already :-(
            availability_ = NULL;
        }
    }
}

