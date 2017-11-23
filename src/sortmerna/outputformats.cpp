/**
 * @file outputformats.cpp
 * @brief File containing functions for outputting results in various formats
 * @parblock
 * SortMeRNA - next-generation reads filter for metatranscriptomic or total RNA
 * @copyright 2012-16 Bonsai Bioinformatics Research Group
 * @copyright 2014-16 Knight Lab, Department of Pediatrics, UCSD, La Jolla
 *
 * SortMeRNA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SortMeRNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with SortMeRNA.  If not, see <http://www.gnu.org/licenses/>.
 * @endparblock
 *
 * @contributors Jenya Kopylova, jenya.kopylov@gmail.com
 *               Laurent Noé, laurent.noe@lifl.fr
 *               Pierre Pericard, pierre.pericard@lifl.fr
 *               Daniel McDonald, wasade@gmail.com
 *               Mikaël Salson, mikael.salson@lifl.fr
 *               Hélène Touzet, helene.touzet@lifl.fr
 *               Rob Knight, robknight@ucsd.edu
 */
#include "outputformats.hpp"
#include "output.hpp"
#include "references.hpp"
#include "read.hpp"
#include "ssw.hpp" // s_align2

using namespace std;

/** @file */

/**
 * Prototype: paralleltraversal lines 1531..1555
 * Calculate Mismatches, Gaps, and ID
 *
 * @param IN Refs  references
 * @param IN Read
 * @param IN alignIdx index into Read.hits_align_info.alignv
 * @param OUT mismatches  calculated here for the given Read Alignment
 * @param OUT gaps
 * @param OUT id
 */
void Output::calcMismatchGapId(References & refs, Read & read, int alignIdx, uint32_t & mismatches, uint32_t & gaps, double & id)
{
	const char to_char[5] = { 'A','C','G','T','N' };

	int32_t qb = read.hits_align_info.alignv[alignIdx].ref_begin1; //ptr_alignment->ref_begin1;
	int32_t pb = read.hits_align_info.alignv[alignIdx].read_begin1; //->read_begin1;

	std::string refseq = refs.buffer[read.hits_align_info.alignv[alignIdx].ref_seq].sequence;

	for (uint32_t c2 = 0; c2 < read.hits_align_info.alignv[alignIdx].cigarLen; ++c2)
	{
		uint32_t letter = 0xf & read.hits_align_info.alignv[alignIdx].cigar[c2]; // 4 low bits
		uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[alignIdx].cigar[c2]) >> 4; // high 28 bits i.e. 32-4=28
		if (letter == 0)
		{
			for (uint32_t u = 0; u < length; ++u)
			{
				if ( (char)to_char[(int)refseq[qb]] != (char)to_char[(int)read.sequence[pb]] ) ++mismatches;
				else ++id;
				++qb;
				++pb;
			}
		}
		else if (letter == 1)
		{
			pb += length;
			gaps += length;
		}
		else
		{
			qb += length;
			gaps += length;
		}
	}
} // ~Output::calcMismatchGapId

void Output::report_blast
	(
		ofstream &fileout,
		Index & index,
		References & refs,
		Read & read
	)
{
	const char to_char[5] = { 'A','C','G','T','N' };
	double id = 0;
	uint32_t mismatches = 0;
	uint32_t gaps = 0;

	// iterate all alignments of the read
	for (int i = 0; i < read.hits_align_info.alignv.size(); ++i)
	{
		uint32_t bitscore = (uint32_t)((float)((index.gumbel[index.index_num].first)
			* (read.hits_align_info.alignv[i].score1) - log(index.gumbel[index.index_num].second)) / (float)log(2));

		double evalue_score = (double)(index.gumbel[index.index_num].second) * index.full_ref[index.index_num]
			* index.full_read[index.index_num]
			* std::exp(-(index.gumbel[index.index_num].first) * read.hits_align_info.alignv[i].score1);

		std::string refseq = refs.buffer[read.hits_align_info.alignv[i].ref_seq].sequence;
		std::string refhead = refs.buffer[read.hits_align_info.alignv[i].ref_seq].header;

		// Blast-like pairwise alignment (only for aligned reads)
		if (index.opts.blastFormat == BlastFormat::REGULAR) // TODO: global - fix
		{
			fileout << "Sequence ID: ";
			//const char* tmp = read.hits_align_info.alignv[alignIdx].ref_seq; // ref_name
			//while (*tmp != '\n') fileout << *tmp++;
			fileout << refhead.substr(0, refhead.find(' ')); // print only start of the header till first space
			fileout << endl;

			fileout << "Query ID: ";
			//tmp = read_name;
			//while (*tmp != '\n') fileout << *tmp++;
			fileout << read.header.substr(0, read.header.find(' '));
			fileout << endl;

			//fileout << "Score: " << a->score1 << " bits (" << bitscore << ")\t";
			fileout << "Score: " << read.hits_align_info.alignv[i].score1 << " bits (" << bitscore << ")\t";
			fileout.precision(3);
			fileout << "Expect: " << evalue << "\t";

			if (read.hits_align_info.alignv[i].strand) fileout << "strand: +\n\n";
			else fileout << "strand: -\n\n";

			if (read.hits_align_info.alignv[i].cigar.size() > 0)
			{
				uint32_t j, c = 0, left = 0, e = 0,
					qb = read.hits_align_info.alignv[i].ref_begin1,
					pb = read.hits_align_info.alignv[i].read_begin1; //mine

				while (e < read.hits_align_info.alignv[i].cigarLen || left > 0)
				{
					int32_t count = 0;
					int32_t q = qb;
					int32_t p = pb;
					fileout << "Target: ";
					fileout.width(8);
					fileout << q + 1 << "    ";
					// process CIGAR
					for (c = e; c < read.hits_align_info.alignv[i].cigarLen; ++c)
					{
						// 4 Low bits encode a Letter: M | D | S
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c]; // *(a->cigar + c)
						// 28 High bits encode the number of occurencies e.g. 34
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4; // *(a->cigar + c)
						uint32_t l = (count == 0 && left > 0) ? left : length;
						for (j = 0; j < l; ++j)
						{
							if (letter == 1) fileout << "-";
							else
							{
								//fileout << to_char[(int)*(refseq + q)];
								fileout << to_char[(int)refseq[q]];
								++q;
							}
							++count;
							if (count == 60) goto step2;
						}
					}
				step2:
					fileout << "    " << q << "\n";
					fileout.width(20);
					fileout << " ";
					q = qb;
					count = 0;
					for (c = e; c < read.hits_align_info.alignv[i].cigarLen; ++c)
					{
						//uint32_t letter = 0xf & *(a->cigar + c);
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c];
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4;
						uint32_t l = (count == 0 && left > 0) ? left : length;
						for (j = 0; j < l; ++j)
						{
							if (letter == 0)
							{
								if ((char)to_char[(int)refseq[q]] == (char)to_char[(int)read.sequence[p]]) fileout << "|";
								else fileout << "*";
								++q;
								++p;
							}
							else
							{
								fileout << " ";
								if (letter == 1) ++p;
								else ++q;
							}
							++count;
							if (count == 60)
							{
								qb = q;
								goto step3;
							}
						}
					}
				step3:
					p = pb;
					fileout << "\nQuery: ";
					fileout.width(9);
					fileout << p + 1 << "    ";
					count = 0;
					for (c = e; c < read.hits_align_info.alignv[i].cigarLen; ++c)
					{
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c];
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4;
						uint32_t l = (count == 0 && left > 0) ? left : length;
						for (j = 0; j < l; ++j)
						{
							if (letter == 2) fileout << "-";
							else
							{
								fileout << (char)to_char[(int)read.sequence[p]];
								++p;
							}
							++count;
							if (count == 60)
							{
								pb = p;
								left = l - j - 1;
								e = (left == 0) ? (c + 1) : c;
								goto end;
							}
						}
					}
					e = c;
					left = 0;
				end:
					fileout << "    " << p << "\n\n";
				}
			}
		}
		// Blast tabular m8 + optional columns for CIGAR and query coverage
		else if (index.opts.blastFormat == BlastFormat::TABULAR)
		{
			// (1) Query
			//while ((*read_name != ' ') && (*read_name != '\n') && (*read_name != '\t')) fileout << (char)*read_name++;
			fileout << read.header.substr(0, read.header.find(' '));

			// print null alignment for non-aligned read
			if (print_all_reads_gv && (read.hits_align_info.alignv.size() == 0))
			{
				fileout << "\t*\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0";
				for (uint32_t l = 0; l < user_opts.size(); l++)
				{
					if (user_opts[l].compare("cigar") == 0)
						fileout << "\t*";
					else if (user_opts[l].compare("qcov") == 0)
						fileout << "\t0";
					else if (user_opts[l].compare("qstrand") == 0)
						fileout << "\t*";
					fileout << "\n";
				}
				return;
			}

			Output::calcMismatchGapId(refs, read, i, mismatches, gaps, id);

			fileout << "\t";
			// (2) Subject
			//while ((*ref_name != ' ') && (*ref_name != '\n') && (*ref_name != '\t')) fileout << (char)*ref_name++;
			fileout << refhead.substr(0, refhead.find(' '));
			fileout << "\t";
			// (3) %id
			fileout.precision(3);
			fileout << id * 100 << "\t";
			// (4) alignment length
			fileout << (read.hits_align_info.alignv[i].read_end1 - read.hits_align_info.alignv[i].read_begin1 + 1) << "\t";
			// (5) mismatches
			fileout << mismatches << "\t";
			// (6) gap openings
			fileout << gaps << "\t";
			// (7) q.start
			fileout << read.hits_align_info.alignv[i].read_begin1 + 1 << "\t";
			// (8) q.end
			fileout << read.hits_align_info.alignv[i].read_end1 + 1 << "\t";
			// (9) s.start
			fileout << read.hits_align_info.alignv[i].ref_begin1 + 1 << "\t";
			// (10) s.end
			fileout << read.hits_align_info.alignv[i].ref_end1 + 1 << "\t";
			// (11) e-value
			fileout << evalue << "\t";
			// (12) bit score
			fileout << bitscore;
			// OPTIONAL columns
			for (uint32_t l = 0; l < user_opts.size(); l++)
			{
				// output CIGAR string
				if (user_opts[l].compare("cigar") == 0)
				{
					fileout << "\t";
					// masked region at beginning of alignment
					if (read.hits_align_info.alignv[i].read_begin1 != 0) fileout << read.hits_align_info.alignv[i].read_begin1 << "S";
					for (int c = 0; c < read.hits_align_info.alignv[i].cigarLen; ++c)
					{
						uint32_t letter = 0xf & read.hits_align_info.alignv[i].cigar[c];
						uint32_t length = (0xfffffff0 & read.hits_align_info.alignv[i].cigar[c]) >> 4;
						fileout << length;
						if (letter == 0) fileout << "M";
						else if (letter == 1) fileout << "I";
						else fileout << "D";
					}

					uint32_t end_mask = read.sequence.length() - read.hits_align_info.alignv[i].read_end1 - 1;
					// output the masked region at end of alignment
					if (end_mask > 0) fileout << end_mask << "S";
				}
				// output % query coverage
				else if (user_opts[l].compare("qcov") == 0)
				{
					fileout << "\t";
					fileout.precision(3);
					double coverage = abs(read.hits_align_info.alignv[i].read_end1 - read.hits_align_info.alignv[i].read_begin1 + 1) 
						/ read.hits_align_info.alignv[i].readlen;
					fileout << coverage * 100; // (double)align_len / readlen
				}
				// output strand
				else if (user_opts[l].compare("qstrand") == 0)
				{
					fileout << "\t";
					if (read.hits_align_info.alignv[i].strand) fileout << "+";
					else fileout << "-";
				}
			}
			fileout << "\n";
		}//~blast tabular m8
	} // ~iterate all alignments
} // ~ Output::report_blast

void Output::report_sam
	(
		ofstream &fileout,
		References & refs,
		Read & read
	)
{
	fileout << "Not implemented\n";
#if 0
	const char to_char[5] = { 'A','C','G','T','N' };
	// (1) Query
	//while ((*read_name != ' ') && (*read_name != '\n') && (*read_name != '\t')) fileout << (char)*read_name++;
	fileout <<  read.header.substr(0, read.header.find(' '));
	// read did not align, output null string
	if (print_all_reads_gv && (a == NULL))
	{
		fileout << "\t4\t*\t0\t0\t*\t*\t0\t0\t*\t*\n";
		return;
	}
	// read aligned, output full alignment
	uint32_t c;
	// (2) flag
	if (!strand) fileout << "\t16\t";
	else fileout << "\t0\t";
	// (3) Subject
	while ((*ref_name != ' ') && (*ref_name != '\n') && (*ref_name != '\t'))
		fileout << (char)*ref_name++;
	// (4) Ref start
	fileout << "\t" << a->ref_begin1 + 1;
	// (5) mapq
	fileout << "\t" << 255 << "\t";
	// (6) CIGAR
	// output the masked region at beginning of alignment
	if (a->read_begin1 != 0) fileout << a->read_begin1 << "S";

	for (c = 0; c < a->cigarLen; ++c) {
		uint32_t letter = 0xf & *(a->cigar + c);
		uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
		fileout << length;
		if (letter == 0) fileout << "M";
		else if (letter == 1) fileout << "I";
		else fileout << "D";
	}
	uint32_t end_mask = readlen - a->read_end1 - 1;
	// output the masked region at end of alignment
	if (end_mask > 0) fileout << end_mask << "S";
	// (7) RNEXT, (8) PNEXT, (9) TLEN
	fileout << "\t*\t0\t0\t";
	// (10) SEQ
	const char* ptr_read_seq = read_seq;
	while (*ptr_read_seq != '\n') fileout << (char)to_char[(int)*ptr_read_seq++];
	// (11) QUAL
	fileout << "\t";
	// reverse-complement strand
	if (read_qual && !strand)
	{
		while (*read_qual != '\n') fileout << (char)*read_qual--;
		// forward strand
	}
	else if (read_qual) {
		while ((*read_qual != '\n') && (*read_qual != '\0')) fileout << (char)*read_qual++;
		// FASTA read
	}
	else fileout << "*";

	// (12) OPTIONAL FIELD: SW alignment score generated by aligner
	fileout << "\tAS:i:" << a->score1;
	// (13) OPTIONAL FIELD: edit distance to the reference
	fileout << "\tNM:i:" << diff << "\n";
#endif
} // ~Output::report_sam


/**
 * output Blast-like alignments (code modified from SSW-library)
 * writes one entry for a single alignment of the read i.e. to write all alignments 
 * this function has to be called multiple times each time changing s_align* pointer.
 */
void report_blast(
	ofstream &fileout,
	s_align* a, // pointer set to the alignment to be written
	const char* read_name,
	const char* read_seq,
	const char* read_qual,
	const char* ref_name,
	const char* ref_seq,
	double evalue,
	uint32_t readlen,
	uint32_t bitscore,
	bool strand, // 1: forward aligned ; 0: reverse complement aligned
	double id, // percentage of identical matches
	double coverage,
	uint32_t mismatches,
	uint32_t gaps
)
{
	char to_char[5] = { 'A','C','G','T','N' };

	// Blast-like pairwise alignment (only for aligned reads)
	if (!blast_tabular)
	{
		fileout << "Sequence ID: ";
		const char* tmp = ref_name;
		while (*tmp != '\n') fileout << *tmp++;
		fileout << endl;

		fileout << "Query ID: ";
		tmp = read_name;
		while (*tmp != '\n') fileout << *tmp++;
		fileout << endl;

		fileout << "Score: " << a->score1 << " bits (" << bitscore << ")\t";
		fileout.precision(3);
		fileout << "Expect: " << evalue << "\t";
		if (strand) fileout << "strand: +\n\n";
		else fileout << "strand: -\n\n";
		if (a->cigar)
		{
			uint32_t i, c = 0, left = 0, e = 0, qb = a->ref_begin1, pb = a->read_begin1; //mine
			while (e < a->cigarLen || left > 0)
			{
				int32_t count = 0;
				int32_t q = qb;
				int32_t p = pb;
				fileout << "Target: ";
				fileout.width(8);
				fileout << q + 1 << "    ";
				for (c = e; c < a->cigarLen; ++c)
				{
					uint32_t letter = 0xf & *(a->cigar + c);
					uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
					uint32_t l = (count == 0 && left > 0) ? left : length;
					for (i = 0; i < l; ++i)
					{
						if (letter == 1) fileout << "-";
						else
						{
							fileout << to_char[(int)*(ref_seq + q)];
							++q;
						}
						++count;
						if (count == 60) goto step2;
					}
				}
			step2:
				fileout << "    " << q << "\n";
				fileout.width(20);
				fileout << " ";
				q = qb;
				count = 0;
				for (c = e; c < a->cigarLen; ++c)
				{
					uint32_t letter = 0xf & *(a->cigar + c);
					uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
					uint32_t l = (count == 0 && left > 0) ? left : length;
					for (i = 0; i < l; ++i)
					{
						if (letter == 0)
						{
							if ((char)to_char[(int)*(ref_seq + q)] == (char)to_char[(int)*(read_seq + p)]) fileout << "|";
							else fileout << "*";
							++q;
							++p;
						}
						else
						{
							fileout << " ";
							if (letter == 1) ++p;
							else ++q;
						}
						++count;
						if (count == 60)
						{
							qb = q;
							goto step3;
						}
					}
				}
			step3:
				p = pb;
				fileout << "\nQuery: ";
				fileout.width(9);
				fileout << p + 1 << "    ";
				count = 0;
				for (c = e; c < a->cigarLen; ++c)
				{
					uint32_t letter = 0xf & *(a->cigar + c);
					uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
					uint32_t l = (count == 0 && left > 0) ? left : length;
					for (i = 0; i < l; ++i)
					{
						if (letter == 2) fileout << "-";
						else
						{
							fileout << (char)to_char[(int)*(read_seq + p)];
							++p;
						}
						++count;
						if (count == 60)
						{
							pb = p;
							left = l - i - 1;
							e = (left == 0) ? (c + 1) : c;
							goto end;
						}
					}
				}
				e = c;
				left = 0;
			end:
				fileout << "    " << p << "\n\n";
			}
		}
	}
	// Blast tabular m8 + optional columns for CIGAR and query coverage
	else
	{
		// (1) Query
		while ((*read_name != ' ') && (*read_name != '\n') && (*read_name != '\t')) fileout << (char)*read_name++;

		// print null alignment for non-aligned read
		if (print_all_reads_gv && (a == NULL))
		{
			fileout << "\t*\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0";
			for (uint32_t l = 0; l < user_opts.size(); l++)
			{
				if (user_opts[l].compare("cigar") == 0)
					fileout << "\t*";
				else if (user_opts[l].compare("qcov") == 0)
					fileout << "\t0";
				else if (user_opts[l].compare("qstrand") == 0)
					fileout << "\t*";
				fileout << "\n";
			}
			return;
		}

		fileout << "\t";
		// (2) Subject
		while ((*ref_name != ' ') && (*ref_name != '\n') && (*ref_name != '\t')) fileout << (char)*ref_name++;
		fileout << "\t";
		// (3) %id
		fileout.precision(3);
		fileout << id * 100 << "\t";
		// (4) alignment length
		fileout << (a->read_end1 - a->read_begin1 + 1) << "\t";
		// (5) mismatches
		fileout << mismatches << "\t";
		// (6) gap openings
		fileout << gaps << "\t";
		// (7) q.start
		fileout << a->read_begin1 + 1 << "\t";
		// (8) q.end
		fileout << a->read_end1 + 1 << "\t";
		// (9) s.start
		fileout << a->ref_begin1 + 1 << "\t";
		// (10) s.end
		fileout << a->ref_end1 + 1 << "\t";
		// (11) e-value
		fileout << evalue << "\t";
		// (12) bit score
		fileout << bitscore;
		// OPTIONAL columns
		for (uint32_t l = 0; l < user_opts.size(); l++)
		{
			// output CIGAR string
			if (user_opts[l].compare("cigar") == 0)
			{
				fileout << "\t";
				// masked region at beginning of alignment
				if (a->read_begin1 != 0) fileout << a->read_begin1 << "S";
				for (int c = 0; c < a->cigarLen; ++c)
				{
					uint32_t letter = 0xf & *(a->cigar + c);
					uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
					fileout << length;
					if (letter == 0) fileout << "M";
					else if (letter == 1) fileout << "I";
					else fileout << "D";
				}

				uint32_t end_mask = readlen - a->read_end1 - 1;
				// output the masked region at end of alignment
				if (end_mask > 0) fileout << end_mask << "S";
			}
			// output % query coverage
			else if (user_opts[l].compare("qcov") == 0)
			{
				fileout << "\t";
				fileout.precision(3);
				fileout << coverage * 100;
			}
			// output strand
			else if (user_opts[l].compare("qstrand") == 0)
			{
				fileout << "\t";
				if (strand) fileout << "+";
				else fileout << "-";
			}
		}
		fileout << "\n";
	}//~blast tabular m8

	return;
} // ~report_blast

// output SAM alignments (code modified from SSW-library)
void report_sam(
	ofstream &fileout,
	s_align* a,
	const char* read_name,
	const char* read_seq,
	const char* read_qual,
	const char* ref_name,
	const char* ref_seq,
	uint32_t readlen,
	bool strand, // 1: forward aligned ; 0: reverse complement aligned
	uint32_t diff
)
{
	const char to_char[5] = { 'A','C','G','T','N' };
	// (1) Query
	while ((*read_name != ' ') && (*read_name != '\n') && (*read_name != '\t')) fileout << (char)*read_name++;
	// read did not align, output null string
	if (print_all_reads_gv && (a == NULL))
	{
		fileout << "\t4\t*\t0\t0\t*\t*\t0\t0\t*\t*\n";
		return;
	}
	// read aligned, output full alignment
	uint32_t c;
	// (2) flag
	if (!strand) fileout << "\t16\t";
	else fileout << "\t0\t";
	// (3) Subject
	while ((*ref_name != ' ') && (*ref_name != '\n') && (*ref_name != '\t'))
		fileout << (char)*ref_name++;
	// (4) Ref start
	fileout << "\t" << a->ref_begin1 + 1;
	// (5) mapq
	fileout << "\t" << 255 << "\t";
	// (6) CIGAR
	// output the masked region at beginning of alignment
	if (a->read_begin1 != 0) fileout << a->read_begin1 << "S";

	for (c = 0; c < a->cigarLen; ++c) {
		uint32_t letter = 0xf & *(a->cigar + c);
		uint32_t length = (0xfffffff0 & *(a->cigar + c)) >> 4;
		fileout << length;
		if (letter == 0) fileout << "M";
		else if (letter == 1) fileout << "I";
		else fileout << "D";
	}
	uint32_t end_mask = readlen - a->read_end1 - 1;
	// output the masked region at end of alignment
	if (end_mask > 0) fileout << end_mask << "S";
	// (7) RNEXT, (8) PNEXT, (9) TLEN
	fileout << "\t*\t0\t0\t";
	// (10) SEQ
	const char* ptr_read_seq = read_seq;
	while (*ptr_read_seq != '\n') fileout << (char)to_char[(int)*ptr_read_seq++];
	// (11) QUAL
	fileout << "\t";
	// reverse-complement strand
	if (read_qual && !strand)
	{
		while (*read_qual != '\n') fileout << (char)*read_qual--;
		// forward strand
	}
	else if (read_qual) {
		while ((*read_qual != '\n') && (*read_qual != '\0')) fileout << (char)*read_qual++;
		// FASTA read
	}
	else fileout << "*";

	// (12) OPTIONAL FIELD: SW alignment score generated by aligner
	fileout << "\tAS:i:" << a->score1;
	// (13) OPTIONAL FIELD: edit distance to the reference
	fileout << "\tNM:i:" << diff << "\n";

	return;
} // ~report_sam


// output aligned and non-aligned reads in FASTA/FASTQ format
void report_fasta(
	char* acceptedstrings,
	char* ptr_filetype_or,
	char* ptr_filetype_ar,
	char** reads,
	uint64_t strs,
	vector<bool>& read_hits,
	uint32_t file_s,
	char* finalnt
)
{
	// for timing different processes
	double s, f;
	// output accepted reads
	if ((ptr_filetype_ar != NULL) && fastxout_gv)
	{
		eprintf("    Writing aligned FASTA/FASTQ ... ");
		TIME(s);
		ofstream acceptedreads;
		if (fastxout_gv)
			acceptedreads.open(acceptedstrings, ios::app | ios::binary);
		// pair-ended reads
		if (pairedin_gv || pairedout_gv)
		{
			// loop through every read, output accepted reads
			for (uint64_t i = 1; i < strs; i += 4)
			{
				char* begin_read = reads[i - 1];
				// either both reads are accepted, or one is accepted and pairedin_gv
				if ((read_hits[i] && read_hits[i + 2]) ||
					((read_hits[i] || read_hits[i + 2]) && pairedin_gv))
				{
					char* end_read = NULL;
					if (file_s > 0)
					{
						// first read (of split-read + paired-read)
						if (i == 1)
						{
							end_read = reads[3];
							while (*end_read != '\0') end_read++;
						}
						// all reads except the last one
						else if ((i + 4) < strs) end_read = reads[i + 3];
						// last read
						else end_read = finalnt;
					}
					else
					{
						// all reads except the last one
						if ((i + 4) < strs) end_read = reads[i + 3];
						// last read
						else end_read = finalnt;
					}
					// output aligned read
					if (fastxout_gv)
					{
						if (acceptedreads.is_open())
						{
							while (begin_read != end_read) acceptedreads << (char)*begin_read++;
							if (*end_read == '\n') acceptedreads << "\n";
						}
						else
						{
							fprintf(stderr, "  %sERROR%s: [Line %d: %s] file %s could not be opened for writing.\n\n",
								"\033[0;31m", "\033[0m", __LINE__, __FILE__, acceptedstrings);
							exit(EXIT_FAILURE);
						}
					}
				}//~the read was accepted
			}//~for all reads
		}//~if paired-in or paired-out
		// regular or pair-ended reads don't need to go into the same file
		else
		{
			// loop through every read, output accepted reads
			for (uint64_t i = 1; i < strs; i += 2)
			{
				char* begin_read = reads[i - 1];
				// the read was accepted
				if (read_hits[i])
				{
					char* end_read = NULL;
					// split-read and paired-read exist at a different location in memory than the mmap
					if (file_s > 0)
					{
						// first read (of split-read + paired-read)
						if (i == 1)
							end_read = reads[2];
						// second read (of split-read + paired-read)
						else if (i == 3)
						{
							end_read = reads[3];
							while (*end_read != '\0')
								end_read++;
						}
						// all reads except the last one
						else if ((i + 2) < strs)
							end_read = reads[i + 1];
						// last read
						else
							end_read = finalnt;
					}
					// the first (and possibly only) file part, all reads are in mmap
					else
					{
						if ((i + 2) < strs)
							end_read = reads[i + 1];
						else
							end_read = finalnt;
					}
					// output aligned read
					if (fastxout_gv)
					{
						if (acceptedreads.is_open())
						{
							while (begin_read != end_read)
								acceptedreads << (char)*begin_read++;
							if (*end_read == '\n')
								acceptedreads << "\n";
						}
						else
						{
							fprintf(stderr, "  %sERROR%s: file %s (acceptedstrings) could not be "
								"opened for writing.\n\n", "\033[0;31m", acceptedstrings, "\033[0m");
							exit(EXIT_FAILURE);
						}
					}
				} //~if read was accepted
			}//~for all reads
		}//~if not paired-in or paired-out
		if (acceptedreads.is_open()) acceptedreads.close();
		TIME(f);
		eprintf(" done [%.2f sec]\n", (f - s));
	}//~if ( ptr_filetype_ar != NULL )
	// output other reads
	if ((ptr_filetype_or != NULL) && fastxout_gv)
	{
		eprintf("    Writing not-aligned FASTA/FASTQ ... ");
		TIME(s);
		ofstream otherreads(ptr_filetype_or, ios::app | ios::binary);
		// pair-ended reads
		if (pairedin_gv || pairedout_gv)
		{
			// loop through every read, output accepted reads
			for (uint64_t i = 1; i < strs; i += 4)
			{
				char* begin_read = reads[i - 1];
				// neither of the reads were accepted, or exactly one was accepted and pairedout_gv
				if ((!read_hits[i] && !read_hits[i + 2]) ||
					((read_hits[i] ^ (read_hits[i + 2]) && pairedout_gv)))
				{
					if (otherreads.is_open())
					{
						char* end_read = NULL;
						if (file_s > 0)
						{
							// first read (of split-read + paired-read)
							if (i == 1)
							{
								end_read = reads[3];
								while (*end_read != '\0') end_read++;
							}
							// all reads except the last one
							else if ((i + 4) < strs) end_read = reads[i + 3];
							// last read
							else end_read = finalnt;
						}
						else
						{
							// all reads except the last one
							if ((i + 4) < strs) end_read = reads[i + 3];
							// last read
							else end_read = finalnt;
						}

						while (begin_read != end_read) otherreads << (char)*begin_read++;
						if (*end_read == '\n') otherreads << "\n";
					}
					else
					{
						fprintf(stderr, "  %sERROR%s: file %s could not be opened for writing.\n\n", "\033[0;31m", ptr_filetype_or, "\033[0m");
						exit(EXIT_FAILURE);
					}
				}//~the read was accepted
			}//~for all reads
		}//~if (pairedin_gv || pairedout_gv)    
		// output reads single
		else
		{
			// loop through every read, output non-accepted reads
			for (uint64_t i = 1; i < strs; i += 2)
			{
				char* begin_read = reads[i - 1];
				// the read was accepted
				if (!read_hits[i])
				{
					// accepted reads file output
					if (otherreads.is_open())
					{
						char* end_read = NULL;
						// split-read and paired-read exist at a different location in memory than the mmap
						if (file_s > 0)
						{
							// first read (of split-read + paired-read)
							if (i == 1) end_read = reads[2];
							// second read (of split-read + paired-read)
							else if (i == 3)
							{
								end_read = reads[3];
								while (*end_read != '\0') end_read++;
							}
							// all reads except the last one
							else if ((i + 2) < strs) end_read = reads[i + 1];
							// last read
							else end_read = finalnt;
						}
						// the first (and possibly only) file part, all reads are in mmap
						else
						{
							if ((i + 2) < strs) end_read = reads[i + 1];
							else end_read = finalnt;
						}
						while (begin_read != end_read) otherreads << (char)*begin_read++;
						if (*end_read == '\n') otherreads << "\n";
					}
					else
					{
						fprintf(stderr, "  %sERROR%s: file %s could not be opened for writing.\n\n", "\033[0;31m", ptr_filetype_or, "\033[0m");
						exit(EXIT_FAILURE);
					}
				}
			}//~for all reads
		}/// if (pairedin_gv || pairedout_gv)
		if (otherreads.is_open()) otherreads.close();
		TIME(f);
		eprintf(" done [%.2f sec]\n", (f - s));
	}//~if ( ptr_filetype_or != NULL )  
	return;
} // ~report_fasta

void report_denovo(
	char *denovo_otus_file,
	char **reads,
	uint64_t strs,
	vector<bool>& read_hits_denovo,
	uint32_t file_s,
	char *finalnt
)
{
	// for timing different processes
	double s, f;

	// output reads with < id% alignment (passing E-value) for de novo clustering
	if (denovo_otus_file != NULL)
	{
		eprintf("    Writing de novo FASTA/FASTQ ... ");
		TIME(s);

		ofstream denovoreads(denovo_otus_file, ios::app | ios::binary);

		// pair-ended reads
		if (pairedin_gv || pairedout_gv)
		{
			// loop through every read, output accepted reads
			for (uint64_t i = 1; i < strs; i += 4)
			{
				char* begin_read = reads[i - 1];

				// either both reads are accepted, or one is accepted and pairedin_gv
				if ((read_hits_denovo[i] || read_hits_denovo[i + 1]) && pairedin_gv)
				{
					char* end_read = NULL;
					if (file_s > 0)
					{
						// first read (of split-read + paired-read)
						if (i == 1)
						{
							end_read = reads[3];
							while (*end_read != '\0') end_read++;
						}
						// all reads except the last one
						else if ((i + 4) < strs) end_read = reads[i + 3];
						// last read
						else end_read = finalnt;
					}
					else
					{
						// all reads except the last one
						if ((i + 4) < strs) end_read = reads[i + 3];
						// last read
						else end_read = finalnt;
					}

					// output aligned read
					if (denovoreads.is_open())
					{
						while (begin_read != end_read) denovoreads << (char)*begin_read++;
						if (*end_read == '\n') denovoreads << "\n";
					}
					else
					{
						fprintf(stderr, "  %sERROR%s: file %s (denovoreads) could not be opened for writing.\n\n", "\033[0;31m", denovo_otus_file, "\033[0m");
						exit(EXIT_FAILURE);
					}
				}//~the read was accepted
			}//~for all reads
		}//~if paired-in or paired-out
		  /// regular or pair-ended reads don't need to go into the same file
		else
		{
			/// loop through every read, output accepted reads
			for (uint64_t i = 1; i < strs; i += 2)
			{
				char* begin_read = reads[i - 1];

				/// the read was accepted
				if (read_hits_denovo[i])
				{
					char* end_read = NULL;
					/// split-read and paired-read exist at a different location in memory than the mmap
					if (file_s > 0)
					{
						/// first read (of split-read + paired-read)
						if (i == 1) end_read = reads[2];
						/// second read (of split-read + paired-read)
						else if (i == 3)
						{
							end_read = reads[3];
							while (*end_read != '\0') end_read++;
						}
						/// all reads except the last one
						else if ((i + 2) < strs) end_read = reads[i + 1];
						/// last read
						else end_read = finalnt;
					}
					/// the first (and possibly only) file part, all reads are in mmap
					else
					{
						if ((i + 2) < strs) end_read = reads[i + 1];
						else end_read = finalnt;
					}

					/// output aligned read
					if (denovoreads.is_open())
					{
						while (begin_read != end_read) denovoreads << (char)*begin_read++;
						if (*end_read == '\n') denovoreads << "\n";
					}
					else
					{
						fprintf(stderr, "  %sERROR%s: file %s (denovoreads) could not be opened for writing.\n\n", "\033[0;31m", denovo_otus_file, "\033[0m");
						exit(EXIT_FAILURE);
					}
				} //~if read was accepted
			}//~for all reads
		}//~if not paired-in or paired-out

		if (denovoreads.is_open()) denovoreads.close();

		TIME(f);
		eprintf(" done [%.2f sec]\n", (f - s));

	}//~if ( ptr_filetype_ar != NULL )

	return;
} // ~report_denovo


// output a biom table
void report_biom(char* biomfile)
{
	ofstream biomout(biomfile, ios::in);

	if (biomout.is_open())
	{
		biomout << "\"id:\"null,";
		biomout << "\"format\": \"Biological Observation Matrix 1.0.0\",";
		biomout << "\"format_url\": \"http://biom-format.org/documentation/format_versions/biom-1.0.html\"";
		biomout << "\"type\": \"OTU table\",";
		biomout << "\"generated_by\": \"SortMeRNA v2.0\",";
		biomout << "\"date\": \"\",";
		biomout << "\"rows\":[";
		biomout << "\"matrix_type\": \"sparse\",";
		biomout << "\"matrix_element_type\": \"int\",";
		biomout << "\"shape\":";
		biomout << "\"data\":";

		biomout.close();
	}

	return;
}












