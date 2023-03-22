/* -*- c++ -*- */
/*
 * Copyright 2013 Christopher D. Kilgour
 * Copyright 2008, 2009 Dominic Spill, Michael Ossmann
 * Copyright 2007 Dominic Spill
 * Copyright 2005, 2006 Free Software Foundation, Inc.
 *
 * This file is part of gr-bluetooth
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "multi_sniffer_impl.h"
#include <string.h>

namespace gr {
  namespace bluetooth_nishant {

	void error_out(const char *s)
	{
		fprintf(stderr, "Error: %s\n", s);
		abort();
	}

    multi_sniffer::sptr
    multi_sniffer::make(double sample_rate, double center_freq,
                        double squelch_threshold, bool tun)
    {
      return gnuradio::get_initial_sptr (new multi_sniffer_impl(sample_rate, center_freq,
                                                                squelch_threshold, tun));
    }

    /*
     * The private constructor
     */
    multi_sniffer_impl::multi_sniffer_impl(double sample_rate, double center_freq,
                                           double squelch_threshold, bool tun)
      : multi_block(sample_rate, center_freq, squelch_threshold),
        gr::sync_block ("bluetooth multi sniffer block",
                       gr::io_signature::make (1, 1, sizeof (gr_complex)),
                       gr::io_signature::make (0, 0, 0))
    {
      d_tun = tun;
      set_symbol_history(312);
      //set_symbol_history(0);

      /* Tun interface */
      if (d_tun) {
        strncpy(d_chan_name, "btbb", sizeof(d_chan_name)-1);
        if ((d_tunfd = mktun(d_chan_name, d_ether_addr)) == -1) {
          fprintf(stderr,
                  "warning: was not able to open TUN device, "
                  "disabling Wireshark interface\n");
          // throw std::runtime_error("cannot open TUN device");
        }
      }
    }

    /*
     * Our virtual destructor.
     */
    multi_sniffer_impl::~multi_sniffer_impl()
    {
    }


    int
    multi_sniffer_impl::work( int                        noutput_items,
                              gr_vector_const_void_star& input_items,
                              gr_vector_void_star&       output_items )
    { 
	    float scale_factor = d_sample_rate/1000000.0;
	    double freq = d_center_freq;
	    double snr = 1.0;
	    bool leok = false; 
	    bool brok = true;
	    int len = noutput_items-1;
	char *symbols = new char[len];
	    /* number of symbols available */
	    if (brok || leok) {
		    int sym_length = history();
		    float demod_out[noutput_items-1];  
		    gr_complex *ch_samps = (gr_complex *) input_items[0];
		    //printf("Start decoding\n");
		    demod(ch_samps, demod_out, noutput_items-1);
		    int sps = (int)d_samples_per_symbol;
		if (brok){
		    decode_classic_bt(demod_out,noutput_items-1,sps,freq,snr);
		}
			
          	if (leok) {
            		char *symp = symbols;
            		int limit = ((len - SYMBOLS_PER_BASIC_RATE_SHORTENED_ACCESS_CODE) < SYMBOLS_PER_BASIC_RATE_SLOT) ? (len - SYMBOLS_PER_BASIC_RATE_SHORTENED_ACCESS_CODE) : SYMBOLS_PER_BASIC_RATE_SLOT;
	    		//printf("limit=%d",limit);
            		int step_counter = 0;
	    		while (limit >= 0) {
              			int i = le_packet::sniff_aa(symp, limit, freq);
	      			//printf("i=%d\n",i);
              			if (i >= 0) {
                			int step = i + SYMBOLS_PER_LOW_ENERGY_PREAMBLE_AA;
					unsigned packet_length = 0;
					char *bd_filname;
					float *bd_vals;
					int packet_flag = 0;
					//printf("symp[%i], len-i = %i\n", i, len-i);
                			//printf("#STARTPACKET:%d\n",i+step_counter);
					aa(&symp[i], len - i, freq, snr,&packet_length,&bd_filname,&packet_flag,&bd_vals);
					//printf("Filename=%s\n",bd_filname);
					//master_counter ++;
                			//printf("Start of packet:%d End of packet:%d\n",i,len-i);
					len   -= step;
					if(step >= sym_length) error_out("Bad step");
                			symp   = &symp[step];
					step_counter += step;
                			limit -= step;
              			}
              			else {
                			break;
              			}
            		}
          	}
          	delete [] symbols;
		//	  delete [] ch_samples;
        }
        else {
  //        delete [] ch_samples;
        }
      //}
      //printf("Cumulative count updated\n");
      d_cumulative_count += (int) d_samples_per_slot;
      
      /* 
       * The runtime system wants to know how many output items we
       * produced, assuming that this is equal to the number of input
       * items consumed.  We tell it that we produced/consumed one
       * time slot of input items so that our next run starts one slot
       * later.
       */

      //printf("# of output samples: %lf\n", d_samples_per_slot);
      return (int) d_samples_per_slot;
    }


int
multi_sniffer_impl::decode_classic_bt(float *stream, int stream_length,int sps, double freq,double snr)
    {
      /* Looks for an AC in the stream */
      int count,sfo;
      int max_distance = 0; // maximum number of bit errors to tolerate in preamble + trailer
	//printf("Hello Stream Length:%d\n",stream_length);
      int num_symbols = (stream_length - stream_length%sps)/sps;
      //printf("Stream length:%d\n",stream_length);
      char symbol_vals[num_symbols];
	bool packet_flag = false;
      for(sfo = 0; sfo < sps; sfo++){
      		//printf("Entering loop\n");
	      for(count = 0; count<num_symbols;count++){
		      //printf("Count:%d\n",sfo + count*sps);
		      symbol_vals[count] = stream[sfo + count*sps]>0.0?1:0;
	      }
	      count = 0;
	      while( count<num_symbols-SYMBOLS_PER_BASIC_RATE_SHORTENED_ACCESS_CODE ) {
		      char * symbols = &symbol_vals[count];
		      //// start of sync word (includes LSB of sync word)
		      uint8_t preamble = classic_packet::air_to_host8( &symbols[0], 5 );
		      // MSB of LAP and 6-bit barker in 7 symbols
        	      uint16_t barker = classic_packet::air_to_host16( &symbols[61], 7 );
        	      if ((classic_packet::PREAMBLE_DISTANCE[preamble] + classic_packet::BARKER_DISTANCE[barker]) <= max_distance) {
          			uint32_t LAP = classic_packet::air_to_host32( &symbols[38], 24 );
          			if (classic_packet::check_ac( symbols, LAP )) {
            				packet_flag = true;
          			}
			      //packet_flag = true;
        	      }
		      if (packet_flag){
		      		packet_flag = false;
				ac(symbols, num_symbols-count, freq, snr);
				count += SYMBOLS_PER_BASIC_RATE_SHORTENED_ACCESS_CODE;
		      }
		      count++;
      	      }
      }
      return -1;
    }
    /* handle AC */
    void
    multi_sniffer_impl::ac(char *symbols, int len, double freq, double snr)
    {
      /* native (local) clock in 625 us */
      uint32_t clkn = (int) (d_cumulative_count / d_samples_per_slot) & 0x7ffffff;
      classic_packet::sptr pkt = classic_packet::make(symbols, len, clkn, freq);
      uint32_t lap = pkt->get_LAP();
	//printf("Blah\n");
      printf("time %6d, snr=%.1f, channel %2d, LAP %06x ",
             clkn, snr, pkt->get_channel( ), lap);

      if (pkt->header_present()) {
        if (!d_basic_rate_piconets[lap]) {
          d_basic_rate_piconets[lap] = basic_rate_piconet::make(lap);
        }
        basic_rate_piconet::sptr pn = d_basic_rate_piconets[lap];

        if (pn->have_clk6() && pn->have_UAP()) {
          decode(pkt, pn, true);
        }
        else {
          discover(pkt, pn);
        }

        /*
         * If this is an inquiry response, saving the piconet state will only
         * cause problems later.
         */
        if (lap == GIAC || lap == LIAC) {
          d_basic_rate_piconets.erase(lap);
        }
      }
      else {
        id(lap);
      }
    }

    /* handle AA */ /*change return type to char - Nishant*/
    void
    multi_sniffer_impl::aa(char *symbols, int len, double freq, double snr,unsigned* pdu_length,char **filname,int *flag,float **vals)
    {
      le_packet::sptr pkt = le_packet::make(symbols, len, freq);
      uint32_t clkn = (int) (d_cumulative_count / d_samples_per_slot) & 0x7ffffff;
	//printf("samples_per_slot:%f\n",d_samples_per_slot);
      printf("time %6d, snr=%.1f, ", clkn, snr);
      //pkt->print( );

      if (pkt->get_index() == true && (pkt->get_PDU_type() == 0 || pkt->get_PDU_type() == 1 || pkt->get_PDU_type() == 2 || pkt->get_PDU_type() == 6))
      {
	      *filname = pkt->get_bd_string();
	      *vals = pkt->get_bd_ints();
	      //printf("File name:%s",filname);
	      *pdu_length = pkt->get_pdu_length();
	      //printf("Filename=%s\n",*filname);
	      //if (pkt->contact_tracing())
	      //{
		//      printf("Contact Tracing: TRUE\n");
          	if(pkt->le_crc_check())
          	{
			printf("CRC1\n");
            		//*flag = 1;
          	}
          	else
          	{
            		printf("CRC0\n");
          	}
	      //}
	      //else
	      //{
	//	      printf("Contact Tracing: FALSE\n");
	  //    }
	      //if(!(strcmp(*filname,"6be991a778d2"))){
	      //if(!(strcmp(*filname,"50185da28e83")) || !(strcmp(*filname,"6b3c1f38b5fe"))){
	      //if(!(strcmp(*filname,"50c901232152")) || !(strcmp(*filname,"5e8ef98b2bb9")) || !(strcmp(*filname,"7ad438bc0540"))){
	      //if(!(strcmp(*filname,"79ccbc2f77a2")) || !(strcmp(*filname,"59fce2b0639b"))){
	      //if(!(strcmp(*filname,"5ac52b09ae39"))){
	      //if(!(strcmp(*filname,"71129a59bb92"))){
	      /*
	      if(!(strcmp(*filname,"4f23444052c3"))){
	      *flag = 1;
	      }*/
      }
      pkt->print();
      if (pkt->header_present()) {
        uint32_t aa = pkt->get_AA( );
        if (!d_low_energy_piconets[aa]) {
          d_low_energy_piconets[aa] = low_energy_piconet::make(aa);
        }
        low_energy_piconet::sptr pn = d_low_energy_piconets[aa];
      }
      else {
        // TODO: log AA
      }
    }

    /* handle ID packet (no header) */
    void multi_sniffer_impl::id(uint32_t lap)
    {
      printf("ID\n");
      if (d_tun) {
        write_interface(d_tunfd, NULL, 0, 0, lap, ETHER_TYPE);
      }
    }

    /* decode packets with headers */
    void multi_sniffer_impl::decode(classic_packet::sptr pkt,
                                    basic_rate_piconet::sptr pn,
                                    bool first_run)
    {
      uint32_t clock; /* CLK of target piconet */

      clock = (pkt->d_clkn + pn->get_offset());
      pkt->set_clock(clock, pn->have_clk27());
      pkt->set_UAP(pn->get_UAP());

      pkt->decode();

      if (pkt->got_payload()) {
        pkt->print();
        if (d_tun) {
          uint64_t addr = (pkt->get_UAP() << 24) | pkt->get_LAP();

          if (pn->have_NAP()) {
            addr |= ((uint64_t) pn->get_NAP()) << 32;
            pkt->set_NAP(pn->get_NAP());
          }

          /* include 9 bytes for meta data & packet header */
          int length = pkt->get_payload_length() + 9;
          char *data = pkt->tun_format();

          write_interface(d_tunfd, (unsigned char *)data, length,
                          0, addr, ETHER_TYPE);
          free(data);
        }
        if (pkt->get_type() == 2)
          fhs(pkt);
      } else if (first_run) {
        printf("lost clock!\n");
        pn->reset();

        /* start rediscovery with this packet */
        discover(pkt, pn);
      } else {
        printf("Giving up on queued packet!\n");
      }
    }

    void multi_sniffer_impl::decode(le_packet::sptr pkt,
                                    low_energy_piconet::sptr pn) {

    }

    /* work on UAP/CLK1-6 discovery */
    void multi_sniffer_impl::discover(classic_packet::sptr pkt,
                                      basic_rate_piconet::sptr pn)
    {
      printf("working on UAP/CLK1-6\n");

      /* store packet for decoding after discovery is complete */
      pn->enqueue(pkt);

      if (pn->UAP_from_header(pkt))
        /* success! decode the stored packets */
        recall(pn);
    }

    void multi_sniffer_impl::discover(le_packet::sptr pkt,
                                      low_energy_piconet::sptr pn) {
    }

    /* decode stored packets */
    void multi_sniffer_impl::recall(basic_rate_piconet::sptr pn)
    {
      packet::sptr pkt;
      printf("Decoding queued packets\n");

      while (pkt = pn->dequeue()) {
        classic_packet::sptr cpkt = boost::dynamic_pointer_cast<classic_packet>(pkt);
        printf("time %6d, channel %2d, LAP %06x ", cpkt->d_clkn,
               cpkt->get_channel(), cpkt->get_LAP());
        decode(cpkt, pn, false);
      }

      printf("Finished decoding queued packets\n");
    }

    void multi_sniffer_impl::recall(low_energy_piconet::sptr pn) {
    }

    /* pull information out of FHS packet */
    void multi_sniffer_impl::fhs(classic_packet::sptr pkt)
    {
      uint32_t lap;
      uint8_t uap;
      uint16_t nap;
      uint32_t clk;
      uint32_t offset;
      basic_rate_piconet::sptr pn;

      /* caller should have checked got_payload() and get_type() */

      lap = pkt->lap_from_fhs();
      uap = pkt->uap_from_fhs();
      nap = pkt->nap_from_fhs();

      /* clk is shifted to put it into units of 625 microseconds */
      clk = pkt->clock_from_fhs() << 1;
      offset = (clk - pkt->d_clkn) & 0x7ffffff;

      printf("FHS contents: BD_ADDR ");

      printf("%2.2x:", (nap >> 8) & 0xff);
      printf("%2.2x:", nap & 0xff);
      printf("%2.2x:", uap);
      printf("%2.2x:", (lap >> 16) & 0xff);
      printf("%2.2x:", (lap >> 8) & 0xff);
      printf("%2.2x", lap & 0xff);

      printf(", CLK %07x\n", clk);

      /* make use of this information from now on */
      if (!d_basic_rate_piconets[lap]) {
        d_basic_rate_piconets[lap] = basic_rate_piconet::make(lap);
      }
      pn = d_basic_rate_piconets[lap];

      pn->set_UAP(uap);
      pn->set_NAP(nap);
      pn->set_offset(offset);
      //FIXME if this is a role switch, the offset can have an error of as
      //much as 1.25 ms
    }

    /*
      void multi_sniffer_impl::dump_hdr(char *symbols)
      {
      int i;
      for (i = 0; i < 72; i++)
      printf("%d", symbols[i]);
      printf("\n");
      for (; i < 126; i++) {
      printf("%d", symbols[i]);
      if (i % 3 == 2)
      printf(" ");
      }
      printf("\n");
      }
    */


  } /* namespace bluetooth */
} /* namespace gr */
