#include <cmath>
#include "structure.h"

#define WindowSize 80
#define SeedExplorationChunk 10000

int QueryChrIdx;
vector<FragPair_t> SeedVec;
static pthread_mutex_t Lock;
vector<AlnBlock_t> AlnBlockVec;
int64_t TotalAlignmentLength = 0, LocalAlignmentNum = 0, SNP_num = 0, IND_num = 0, SVS_num = 0;
string LineColorArr[10] = {"red", "blue", "web-green", "dark-magenta", "orange", "yellow", "turquoise", "dark-yellow", "violet", "dark-grey"};

bool CompByPosDiff(const FragPair_t& p1, const FragPair_t& p2)
{
	if (p1.PosDiff == p2.PosDiff) return p1.qPos < p2.qPos;
	else return p1.PosDiff < p2.PosDiff;
}

bool CompByQueryPos(const FragPair_t& p1, const FragPair_t& p2)
{
	if (p1.qPos == p2.qPos) return p1.rPos < p2.rPos;
	else return p1.qPos < p2.qPos;
}

bool CompByRefPos(const FragPair_t& p1, const FragPair_t& p2)
{
	if (p1.rPos == p2.rPos) return p1.qPos < p2.qPos;
	else return p1.rPos < p2.rPos;
}

bool CompByChrScore(const pair<int, int64_t>& p1, const pair<int, int64_t>& p2)
{
	return p1.second > p2.second;
}

int CountIdenticalPairs(string& aln1, string& aln2)
{
	int i, n, len = (int)aln1.length();

	for (n = len, i = 0; i < len; i++)
	{
		if (nst_nt4_table[aln1[i]] != nst_nt4_table[aln2[i]]) n--;
	}
	return n;
}

void ShowAlnBlockDistance(int idx1, int idx2)
{
	int n, qPos1, qPos2;
	int64_t rPos1, rPos2;

	n = (int)AlnBlockVec[idx1].FragPairVec.size(); 
	qPos1 = AlnBlockVec[idx1].FragPairVec[n - 1].qPos + AlnBlockVec[idx1].FragPairVec[n - 1].qLen - 1;
	rPos1 = AlnBlockVec[idx1].FragPairVec[n - 1].rPos + AlnBlockVec[idx1].FragPairVec[n - 1].rLen - 1;

	qPos2 = AlnBlockVec[idx2].FragPairVec[0].qPos;
	rPos2 = AlnBlockVec[idx2].FragPairVec[0].rPos;

	printf("qDistance = %d, rDistance = %lld\n", qPos2 - qPos1, rPos2 - rPos1);
}

void ShowFragSeqs(FragPair_t& FragPair)
{
	char *frag1, *frag2;

	frag1 = new char[FragPair.qLen + 1]; strncpy(frag1, QueryChrVec[QueryChrIdx].seq.c_str() + FragPair.qPos, FragPair.qLen); frag1[FragPair.qLen] = '\0';
	frag2 = new char[FragPair.rLen + 1]; strncpy(frag2, RefSequence + FragPair.rPos, FragPair.rLen); frag2[FragPair.rLen] = '\0';

	printf("q[%d-%d]=%d r[%lld-%lld]=%d\n%s\n%s\n\n", FragPair.qPos, FragPair.qPos + FragPair.qLen - 1, FragPair.qLen, FragPair.rPos, FragPair.rPos + FragPair.rLen - 1, FragPair.rLen, frag1, frag2);
}

void ShowFragPair(FragPair_t& FragPair)
{
	printf("q[%d-%d]=%d r[%lld-%lld]=%d\n", FragPair.qPos, FragPair.qPos + FragPair.qLen - 1, FragPair.qLen, FragPair.rPos, FragPair.rPos + FragPair.rLen - 1, FragPair.rLen);
}

void ShowFragPairVec(vector<FragPair_t>& FragPairVec)
{
	printf("FragPairVec (N=%d)\n", (int)FragPairVec.size());
	for (vector<FragPair_t>::iterator iter = FragPairVec.begin(); iter != FragPairVec.end(); iter++)
	{
		if(iter->bSeed)
		{
			printf("\t\tq[%d-%d] r[%lld-%lld] PosDiff=%lld, len=%d\n", iter->qPos, iter->qPos + iter->qLen - 1, iter->rPos, iter->rPos + iter->rLen - 1, iter->PosDiff, iter->qLen);
			//char *frag = new char[iter->qLen + 1];
			//strncpy(frag, QueryChrVec[QueryChrIdx].seq.c_str() + iter->qPos, iter->qLen); frag[iter->qLen] = '\0';
			//printf("\t\t%s\n", frag);
			//delete[] frag;
		}
		else
		{
			printf("\t\tq[%d-%d]=%d r[%lld-%lld]=%d\n", iter->qPos, iter->qPos + iter->qLen - 1, iter->qLen, iter->rPos, iter->rPos + iter->rLen - 1, iter->rLen);
			printf("\t\t%s\n\t\t%s\n", iter->aln1.c_str(), iter->aln2.c_str());
		}
	}
	printf("\n\n");
}

void ShowAlnBlock(int idx)
{
	int qBeg, qEnd;
	int64_t rBeg, rEnd;

	AlnBlock_t& AlnBlock = AlnBlockVec[idx];

	int i, num = (int)AlnBlock.FragPairVec.size();

	printf("AlnBlock: score = %d\n", AlnBlock.score);
	if (idx > 0) ShowAlnBlockDistance(idx - 1, idx);

	qBeg = AlnBlock.FragPairVec[0].qPos; qEnd = AlnBlock.FragPairVec[num - 1].qPos + AlnBlock.FragPairVec[num - 1].qLen - 1;
	rBeg = AlnBlock.FragPairVec[0].rPos; rEnd = AlnBlock.FragPairVec[num - 1].rPos + AlnBlock.FragPairVec[num - 1].rLen - 1;

	printf("q[%d-%d]=%d r[%lld-%lld]=%lld\n", qBeg, qEnd, qEnd - qBeg + 1, rBeg, rEnd, rEnd - rBeg + 1);
	
	//ShowFragPairVec(AlnBlock.FragPairVec);

	printf("\n\n\n");
}

void *IdentifyLocalMEM(void *arg)
{
	FragPair_t seed;
	vector<FragPair_t> vec;
	bwtSearchResult_t bwtSearchResult;
	int i, pos, start, stop, num, len, *my_id = (int*)arg;

	string& seq = QueryChrVec[QueryChrIdx].seq; len = (int)seq.length(); seed.bSeed = true;
	for (pos = (*my_id*SeedExplorationChunk); pos < len; pos += (iThreadNum * SeedExplorationChunk))
	{
		start = pos; if((stop = start + SeedExplorationChunk) > len) stop = len;
		while (start < stop)
		{
			if (nst_nt4_table[(int)seq[start]] > 3) start++;
			else
			{
				bwtSearchResult = BWT_Search(seq, start, stop);
				if (bwtSearchResult.freq > 0)
				{
					seed.rLen = seed.qLen = bwtSearchResult.len; seed.qPos = start;
					for (i = 0; i != bwtSearchResult.freq; i++)
					{
						seed.rPos = bwtSearchResult.LocArr[i];
						seed.PosDiff = seed.rPos - seed.qPos;
						vec.push_back(seed);
					}
					delete[] bwtSearchResult.LocArr;
				}
				start += (bwtSearchResult.len + 1);
			}
		}
		if (*my_id == 0) fprintf(stderr, "\r\t\tSeed exploration: %d / %d (%d%%)...", stop, len, (int)(100 * ((1.0*stop / len))));
	}
	if (*my_id == 0) fprintf(stderr, "\r\t\tSeed exploration: %d / %d (100%%)...done!\n", len, len);
	std::sort(vec.begin(), vec.end(), CompByQueryPos);

	if (iThreadNum == 1) SeedVec.swap(vec);
	else
	{
		pthread_mutex_lock(&Lock);
		num = (int)SeedVec.size();
		copy(vec.begin(), vec.end(), back_inserter(SeedVec));
		inplace_merge(SeedVec.begin(), SeedVec.begin() + num, SeedVec.end(), CompByQueryPos);
		pthread_mutex_unlock(&Lock);
	}

	return (void*)(1);
}

int CalAlnBlockScore(vector<FragPair_t>& FragPairVec)
{
	int score = 0;
	for (vector<FragPair_t>::iterator iter = FragPairVec.begin(); iter != FragPairVec.end(); iter++) score += iter->qLen;

	return score;
}

void AlignmentBlockClustering()
{
	bool *VisitArr;
	int64_t curTail;
	AlnBlock_t AlnBlock;
	int i, j, ci, ext_size, num;

	num = (int)SeedVec.size() + 1; SeedVec.resize(num); SeedVec[num - 1].PosDiff = 0;
	VisitArr = new bool[num](); VisitArr[num - 1] = true;
	for (i = 0; i < num; i++)
	{
		if (VisitArr[i] == false)
		{
			//printf("ClusterHead (%d/%d): q[%d-%d] r[%lld-%lld] len=%d PD=%lld\n", i+1, num, SeedVec[i].qPos, SeedVec[i].qPos + SeedVec[i].qLen - 1, SeedVec[i].rPos, SeedVec[i].rPos + SeedVec[i].rLen - 1, SeedVec[i].qLen, SeedVec[i].PosDiff);
			curTail = SeedVec[i].qPos + SeedVec[i].qLen; VisitArr[i] = true; AlnBlock.FragPairVec.clear(); AlnBlock.FragPairVec.push_back(SeedVec[i]);
			for (ci = i, j = i + 1; j < num; j++)
			{
				if (VisitArr[j] == false)
				{
					if ((abs(SeedVec[j].PosDiff - SeedVec[ci].PosDiff)) < MaxGapSize && (SeedVec[j].qPos - curTail) < MaxGapSize)
					{
						AlnBlock.FragPairVec.push_back(SeedVec[j]);
						ci = j; VisitArr[j] = true; curTail = SeedVec[j].qPos + SeedVec[j].qLen;
						//printf("\tAdd %d: q[%d-%d] r[%lld-%lld] len=%d PD=%lld\n", j + 1, SeedVec[j].qPos, SeedVec[j].qPos + SeedVec[j].qLen - 1, SeedVec[j].rPos, SeedVec[j].rPos + SeedVec[j].rLen - 1, SeedVec[j].qLen, SeedVec[j].PosDiff);
					}
				}
				if (j - ci > 100) break;
			}
			i = ci;
			if ((AlnBlock.score = CalAlnBlockScore(AlnBlock.FragPairVec)) > MinClusterSize) AlnBlockVec.push_back(AlnBlock);
		}
	}
	delete[] VisitArr;
}

bool RemoveOverlaps(vector<FragPair_t>& FragPairVec)
{
	bool bNullPair;
	bool bShowMsg = false;
	int i, j, overlap_size, num = (int)FragPairVec.size();

	//if (FragPairVec[0].qPos == 3048894) bShowMsg = true;
	//if (bShowMsg) printf("Before removing overlaps\n"), ShowFragPairVec(FragPairVec);
	for (i = 0; i < num; i++)
	{
		if (FragPairVec[i].qLen == 0) continue;

		for (j = i + 1; j < num; j++)
		{
			if (FragPairVec[j].qLen == 0) continue;
			else if (FragPairVec[j].PosDiff == FragPairVec[i].PosDiff) break;
			else if (FragPairVec[j].qPos == FragPairVec[i].qPos && FragPairVec[j].qLen == FragPairVec[i].qLen) // repeats
			{
				FragPairVec[j].qLen = FragPairVec[j].rLen = 0, bNullPair = true;
			}
			else if (FragPairVec[j].rPos <= FragPairVec[i].rPos)
			{
				FragPairVec[j].qLen = FragPairVec[j].rLen = 0, bNullPair = true;
			}
			else if ((overlap_size = FragPairVec[i].rPos + FragPairVec[i].rLen - FragPairVec[j].rPos) > 0)
			{
				if (overlap_size >= FragPairVec[j].rLen)
				{
					FragPairVec[j].qLen = FragPairVec[j].rLen = 0, bNullPair = true;
				}
				else if (overlap_size >= FragPairVec[i].rLen)
				{
					FragPairVec[i].qLen = FragPairVec[i].rLen = 0, bNullPair = true; break;
				}
				else FragPairVec[i].rLen = FragPairVec[i].qLen -= overlap_size; // shrink block i
			}
			else if ((overlap_size = FragPairVec[i].qPos + FragPairVec[i].qLen - FragPairVec[j].qPos) > 0)
			{
				if (overlap_size >= FragPairVec[j].qLen)
				{
					FragPairVec[j].qLen = FragPairVec[j].rLen = 0, bNullPair = true;
				}
				else if(overlap_size >= FragPairVec[i].qLen)
				{
					FragPairVec[i].qLen = FragPairVec[i].rLen = 0, bNullPair = true; break;
				}
				else FragPairVec[i].rLen = FragPairVec[i].qLen -= overlap_size; // shrink block i
			}
			else break;
		}
	}
	//if (bShowMsg) printf("After removing overlaps\n"), ShowFragPairVec(FragPairVec);
	return bNullPair;
}

void RemoveNullFragPairs(vector<FragPair_t>& FragPairVec)
{
	vector<FragPair_t> vec;

	vec.reserve((int)FragPairVec.size());
	for (vector<FragPair_t>::iterator iter = FragPairVec.begin(); iter != FragPairVec.end(); iter++) if (iter->qLen > 0) vec.push_back(*iter);
	FragPairVec.swap(vec);
}

void IdentifyNormalPairs(vector<FragPair_t>& FragPairVec)
{
	FragPair_t FragPair;
	int i, j, qGaps, rGaps, num = (int)FragPairVec.size();

	FragPair.bSeed = false;
	for (i = 0, j = 1; j < num; i++, j++)
	{
		if ((qGaps = FragPairVec[j].qPos - (FragPairVec[i].qPos + FragPairVec[i].qLen)) < 0) qGaps = 0;
		if ((rGaps = FragPairVec[j].rPos - (FragPairVec[i].rPos + FragPairVec[i].rLen)) < 0) rGaps = 0;

		if (qGaps > 0 || rGaps > 0)
		{
			FragPair.qPos = FragPairVec[i].qPos + FragPairVec[i].qLen;
			FragPair.rPos = FragPairVec[i].rPos + FragPairVec[i].rLen;
			FragPair.PosDiff = FragPair.rPos - FragPair.qPos;
			FragPair.qLen = qGaps; FragPair.rLen = rGaps;
			FragPairVec.push_back(FragPair);
			//if (qGaps > MinSeedLength || rGaps > MinSeedLength) ShowFragSeqs(FragPair);
			//printf("insert a normal pair: r[%d-%d] g[%lld-%lld] and r[%d-%d] g[%lld-%lld]: r[%d-%d] g[%lld-%lld]\n", FragPairVec[i].rPos, FragPairVec[i].rPos + FragPairVec[i].rLen - 1, FragPairVec[i].gPos, FragPairVec[i].gPos + FragPairVec[i].gLen - 1, FragPairVec[j].rPos, FragPairVec[j].rPos + FragPairVec[j].rLen - 1, FragPairVec[j].gPos, FragPairVec[j].gPos + FragPairVec[j].gLen - 1, FragPair.rPos, FragPair.rPos + FragPair.rLen - 1, FragPair.gPos, FragPair.gPos + FragPair.gLen - 1);
		}
	}
	if ((int)FragPairVec.size() > num) inplace_merge(FragPairVec.begin(), FragPairVec.begin() + num, FragPairVec.end(), CompByQueryPos);
}

void CheckFragPairContinuity(vector<FragPair_t>& FragPairVec)
{
	int64_t rPos;
	bool bChecked = true;
	int i, qPos, num = (int)FragPairVec.size();

	if (num == 0) return;

	qPos = FragPairVec[0].qPos + FragPairVec[0].qLen;
	rPos = FragPairVec[0].rPos + FragPairVec[0].rLen;

	for (i = 1; i < num; i++)
	{
		if (FragPairVec[i].qPos != qPos || FragPairVec[i].rPos != rPos)
		{
			printf("Stop!\n"); ShowFragSeqs(FragPairVec[i]);
			bChecked = false;
			break;
		}
		else
		{
			qPos = FragPairVec[i].qPos + FragPairVec[i].qLen;
			rPos = FragPairVec[i].rPos + FragPairVec[i].rLen;
		}
	}
}

int CheckFragPairMismatch(FragPair_t* FragPair)
{
	int i, mismatch = 0;
	char *TemplateSeq = RefSequence + FragPair->rPos;
	char *QuerySeq = (char*)QueryChrVec[QueryChrIdx].seq.c_str() + FragPair->qPos;

	for (i = 0; i < FragPair->qLen; i++)
	{
		if (QuerySeq[i] != TemplateSeq[i] && QuerySeq[i] != 'N' && TemplateSeq[i] != 'N') mismatch++;
	}
	return mismatch;
}

void *GenerateFragAlignment(void *arg)
{
	FragPair_t *FragPair;
	int i, j, AlnBlockNum, FragPairNum, TailIdx, mismatch, *my_id = (int*)arg;

	AlnBlockNum = (int)AlnBlockVec.size();

	for (i = *my_id; i < AlnBlockNum; i+= iThreadNum)
	{
		AlnBlockVec[i].score = AlnBlockVec[i].aln_len = 0;
		FragPairNum = (int)AlnBlockVec[i].FragPairVec.size(); TailIdx = FragPairNum - 1;
		for (j = 0; j < FragPairNum; j++)
		{
			if (AlnBlockVec[i].FragPairVec[j].bSeed)
			{
				AlnBlockVec[i].score += AlnBlockVec[i].FragPairVec[j].qLen;
				AlnBlockVec[i].aln_len += AlnBlockVec[i].FragPairVec[j].qLen;
				continue;
			}
			FragPair = &AlnBlockVec[i].FragPairVec[j];
			if (FragPair->qLen == 0)
			{
				AlnBlockVec[i].aln_len += FragPair->rLen;
				FragPair->aln1.resize(FragPair->rLen); strncpy((char*)FragPair->aln1.c_str(), RefSequence + FragPair->rPos, FragPair->rLen);
				FragPair->aln2.assign(FragPair->rLen, '-');
			}
			else if (FragPair->rLen == 0)
			{
				AlnBlockVec[i].aln_len += FragPair->qLen;
				FragPair->aln1.assign(FragPair->qLen, '-');
				FragPair->aln2.resize(FragPair->qLen); strncpy((char*)FragPair->aln2.c_str(), QueryChrVec[QueryChrIdx].seq.c_str() + FragPair->qPos, FragPair->qLen);
			}
			else if (FragPair->qLen == FragPair->rLen && (mismatch = CheckFragPairMismatch(FragPair)) <= 5)
			{
				FragPair->aln1.resize(FragPair->rLen); strncpy((char*)FragPair->aln1.c_str(), RefSequence + FragPair->rPos, FragPair->rLen);
				FragPair->aln2.resize(FragPair->qLen); strncpy((char*)FragPair->aln2.c_str(), QueryChrVec[QueryChrIdx].seq.c_str() + FragPair->qPos, FragPair->qLen);
				AlnBlockVec[i].score += CountIdenticalPairs(FragPair->aln1, FragPair->aln2);
				AlnBlockVec[i].aln_len += FragPair->qLen;
			}
			else
			{
				if (bDebugMode) printf("GenAln: %d vs %d\n", FragPair->qLen, FragPair->rLen), fflush(stdout);
				FragPair->aln1.resize(FragPair->rLen); strncpy((char*)FragPair->aln1.c_str(), RefSequence + FragPair->rPos, FragPair->rLen);
				FragPair->aln2.resize(FragPair->qLen); strncpy((char*)FragPair->aln2.c_str(), QueryChrVec[QueryChrIdx].seq.c_str() + FragPair->qPos, FragPair->qLen);
				
				nw_alignment(FragPair->rLen, FragPair->aln1, FragPair->qLen, FragPair->aln2);
				AlnBlockVec[i].score += CountIdenticalPairs(FragPair->aln1, FragPair->aln2);
				AlnBlockVec[i].aln_len += (int)FragPair->aln1.length();
			}

			if (j == 0 && !AlnBlockVec[i].FragPairVec[j].bSeed)
			{
				int k, rGaps = 0, qGaps = 0;
				for (k = 0; k < (int)FragPair->aln1.length(); k++)
				{
					if (FragPair->aln1[k] == '-') rGaps++;
					else if (FragPair->aln2[k] == '-') qGaps++;
					else break;
				}
				if(k > 0)
				{
					FragPair->aln1 = FragPair->aln1.substr(k);
					FragPair->aln2 = FragPair->aln2.substr(k);
					FragPair->rLen -= rGaps; FragPair->rPos += rGaps;
					FragPair->qLen -= qGaps; FragPair->qPos += qGaps;
					AlnBlockVec[i].aln_len -= (rGaps + qGaps);
				}
			}
			else if (j == TailIdx && !AlnBlockVec[i].FragPairVec[j].bSeed)
			{
				int k, rGaps = 0, qGaps = 0;
				for (k = FragPair->aln1.length() - 1; k >= 0; k--)
				{
					if (FragPair->aln1[k] == '-') rGaps++;
					else if (FragPair->aln2[k] == '-') qGaps++;
					else break;
				}
				if (++k < FragPair->aln1.length())
				{
					FragPair->aln1.resize(k);
					FragPair->aln2.resize(k);
					FragPair->rLen -= rGaps;
					FragPair->qLen -= qGaps;
					AlnBlockVec[i].aln_len -= (rGaps + qGaps);
				}
			}
		}
		if (AlnBlockVec[i].aln_len < MinAlnLength || (int)(100 * (1.0*AlnBlockVec[i].score / AlnBlockVec[i].aln_len)) < MinSeqIdy) AlnBlockVec[i].score = 0;
	}
	return (void*)(1);
}

Coordinate_t GenCoordinateInfo(int64_t rPos)
{
	Coordinate_t coordinate;
	map<int64_t, int>::iterator iter;

	if (rPos < GenomeSize)
	{
		coordinate.bDir = true;

		iter = ChrLocMap.lower_bound(rPos);
		coordinate.ChromosomeIdx = iter->second;
		coordinate.gPos = rPos + 1 - ChromosomeVec[iter->second].FowardLocation;
	}
	else
	{
		coordinate.bDir = false;

		iter = ChrLocMap.lower_bound(rPos);
		coordinate.ChromosomeIdx = iter->second;
		coordinate.gPos = iter->first - rPos + 1;
	}
	return coordinate;
}

int CountBaseNum(string& frag)
{
	int n = (int)frag.length();

	for (string::iterator iter = frag.begin(); iter != frag.end(); iter++) if (*iter == '-') n--;

	return n;
}

void OutputAlignment()
{
	char* frag;
	FILE *outFile;
	int64_t RefPos;
	int i, p, q, RefIdx, QueryPos;
	vector<FragPair_t>::iterator FragPairIter;
	string QueryChrName, RefChrName, aln1, aln2, frag1, frag2;

	outFile = fopen(alnFileName, "a"); 
	for (vector<AlnBlock_t>::iterator ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++)
	{
		if (ABiter->score == 0) continue;

		aln1.clear(); aln2.clear();
		for (FragPairIter = ABiter->FragPairVec.begin(); FragPairIter != ABiter->FragPairVec.end(); FragPairIter++)
		{
			if (FragPairIter->bSeed)
			{
				frag = new char[FragPairIter->qLen + 1]; frag[FragPairIter->qLen] = '\0';
				strncpy(frag, QueryChrVec[QueryChrIdx].seq.c_str() + FragPairIter->qPos, FragPairIter->qLen);
				aln1 += frag; aln2 += frag; delete[] frag;
			}
			else
			{
				aln1 += FragPairIter->aln1;
				aln2 += FragPairIter->aln2;
			}
		}
		LocalAlignmentNum++; TotalAlignmentLength += ABiter->aln_len;
		RefIdx = ABiter->coor.ChromosomeIdx;
		QueryChrName = QueryChrVec[QueryChrIdx].name; RefChrName = ChromosomeVec[RefIdx].name;
		if (QueryChrName.length() > RefChrName.length()) RefChrName += string().assign((QueryChrName.length() - RefChrName.length()), ' ');
		else QueryChrName += string().assign((RefChrName.length() - QueryChrName.length()), ' ');

		fprintf(outFile, "#Identity = %d / %d (%.2f%%) Orientation = %s\n\n", ABiter->score, ABiter->aln_len, (int)(10000 * (1.0*ABiter->score / ABiter->aln_len)) / 100.0, ABiter->coor.bDir? "Forward":"Reverse");
		//ShowFragPairVec(ABiter->FragPairVec); printf("\n\n");
		i = 0; QueryPos = ABiter->FragPairVec[0].qPos + 1; RefPos = ABiter->coor.gPos;
		while (i < ABiter->aln_len)
		{
			frag1 = aln1.substr(i, WindowSize); frag2 = aln2.substr(i, WindowSize);
			p = CountBaseNum(frag1); q = CountBaseNum(frag2);

			fprintf(outFile, "%s\t%12d\t%s\n%s\t%12d\t%s\n\n", RefChrName.c_str(), RefPos, frag1.c_str(), QueryChrName.c_str(), QueryPos, frag2.c_str());
			i += WindowSize; RefPos += (ABiter->coor.bDir ? p : 0 - p); QueryPos += q;
		}
		fprintf(outFile, "%s\n", string().assign(100, '*').c_str());
	}
	std::fclose(outFile);
	//printf("Total alignment length = %lld\n", TotalAlnLen);
}

void OutputMAF()
{
	char* frag;
	FILE *outFile;
	int i, p, q, RefIdx, ns;
	vector<FragPair_t>::iterator FragPairIter;
	string QueryChrName, RefChrName, aln1, aln2, frag1, frag2;

	outFile = fopen(mafFileName, "a");
	for (vector<AlnBlock_t>::iterator ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++)
	{
		if (ABiter->score == 0) continue;

		aln1.clear(); aln2.clear();
		for (FragPairIter = ABiter->FragPairVec.begin(); FragPairIter != ABiter->FragPairVec.end(); FragPairIter++)
		{
			if (FragPairIter->bSeed)
			{
				frag = new char[FragPairIter->qLen + 1]; frag[FragPairIter->qLen] = '\0';
				strncpy(frag, QueryChrVec[QueryChrIdx].seq.c_str() + FragPairIter->qPos, FragPairIter->qLen);
				aln1 += frag; aln2 += frag; delete[] frag;
			}
			else
			{
				aln1 += FragPairIter->aln1;
				aln2 += FragPairIter->aln2;
			}
		}
		LocalAlignmentNum++; TotalAlignmentLength += ABiter->aln_len;
		RefIdx = ABiter->coor.ChromosomeIdx;
		QueryChrName = QueryChrVec[QueryChrIdx].name; RefChrName = ChromosomeVec[RefIdx].name;
		if (QueryChrName.length() > RefChrName.length()) RefChrName += string().assign((QueryChrName.length() - RefChrName.length()), ' ');
		else QueryChrName += string().assign((RefChrName.length() - QueryChrName.length()), ' ');

		if (ABiter->coor.bDir)
		{
			fprintf(outFile, "a score=%d\n", ABiter->score);
			fprintf(outFile, "s %s %lld %lld + %d %s\n", ChromosomeVec[RefIdx].name, ABiter->coor.gPos, (long long)aln1.length(), ChromosomeVec[RefIdx].len, (char*)aln1.c_str());
			fprintf(outFile, "s %s %lld %lld + %d %s\n\n", (char*)QueryChrName.c_str(), ABiter->FragPairVec[0].qPos + 1, (long long)aln2.length(), (int)QueryChrVec[QueryChrIdx].seq.length(), (char*)aln2.c_str());
		}
		else
		{
			//ShowFragPairVec(ABiter->FragPairVec);
			i = (int)ABiter->FragPairVec.size() - 1;
			int64_t rPos = ABiter->FragPairVec[i].rPos + ABiter->FragPairVec[i].rLen - 1;
			SelfComplementarySeq((int)aln1.length(), (char*)aln1.c_str());
			SelfComplementarySeq((int)aln2.length(), (char*)aln2.c_str());
			fprintf(outFile, "a score=%d\n", ABiter->score);
			fprintf(outFile, "s %s %lld %lld + %d %s\n", ChromosomeVec[RefIdx].name, GenCoordinateInfo(rPos).gPos, (long long)aln1.length(), ChromosomeVec[RefIdx].len, (char*)aln1.c_str());
			fprintf(outFile, "s %s %lld %lld - %d %s\n\n", (char*)QueryChrName.c_str(), (int)QueryChrVec[QueryChrIdx].seq.length() - (ABiter->FragPairVec[i].qPos + ABiter->FragPairVec[i].qLen - 1), (long long)aln2.length(), (int)QueryChrVec[QueryChrIdx].seq.length(), (char*)aln2.c_str());
		}
	}
	std::fclose(outFile);
}

void OutputVariantCallingFile()
{
	int64_t rPos;
	FILE *outFile;
	string RefChrName, frag1, frag2;
	int i, j, qPos, aln_len, ind_len;
	vector<FragPair_t>::iterator FragPairIter;

	outFile = fopen(vcfFileName, "a");
	for (vector<AlnBlock_t>::iterator ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++)
	{
		if (!ABiter->coor.bDir || ABiter->score == 0) continue;

		RefChrName = ChromosomeVec[ABiter->coor.ChromosomeIdx].name;

		for (FragPairIter = ABiter->FragPairVec.begin(); FragPairIter != ABiter->FragPairVec.end(); FragPairIter++)
		{
			if (!FragPairIter->bSeed)
			{
				if (FragPairIter->qLen == 0) // delete
				{
					frag1.resize(FragPairIter->rLen + 1); strncpy((char*)frag1.c_str(), RefSequence + FragPairIter->rPos - 1, FragPairIter->rLen + 1);
					fprintf(outFile, "%s\t%d\t.\t%s\t%c\t100\tPASS\tmt=DELETE\n", RefChrName.c_str(), GenCoordinateInfo(FragPairIter->rPos - 1).gPos, (char*)frag1.c_str(), QueryChrVec[QueryChrIdx].seq[FragPairIter->qPos - 1]);
				}
				else if (FragPairIter->rLen == 0) // insert
				{
					frag2.resize(FragPairIter->qLen + 1); strncpy((char*)frag2.c_str(), QueryChrVec[QueryChrIdx].seq.c_str() + FragPairIter->qPos - 1, FragPairIter->qLen + 1);
					fprintf(outFile, "%s\t%d\t.\t%c\t%s\t100\tPASS\tmt=INSERT\n", RefChrName.c_str(), GenCoordinateInfo(FragPairIter->rPos - 1).gPos, RefSequence[FragPairIter->rPos - 1], (char*)frag2.c_str());
				}
				else if (FragPairIter->qLen == 1 && FragPairIter->rLen == 1 && nst_nt4_table[FragPairIter->aln1[0]] != nst_nt4_table[FragPairIter->aln2[0]]) // substitution
				{
					fprintf(outFile, "%s\t%d\t.\t%c\t%c\t100\tPASS\tmt=SUBSTITUTE\n", RefChrName.c_str(), GenCoordinateInfo(FragPairIter->rPos).gPos, FragPairIter->aln1[0], FragPairIter->aln2[0]);
				}
				else
				{
					//fprintf(stdout, "ref=%s\nqry=%s\n", FragPairIter->aln2.c_str(), FragPairIter->aln1.c_str());
					for (aln_len = (int)FragPairIter->aln1.length(), rPos = FragPairIter->rPos, qPos = FragPairIter->qPos, i = 0; i < aln_len; i++)
					{
						if (FragPairIter->aln1[i] == FragPairIter->aln2[i])
						{
							rPos++; qPos++;
						}
						else if (FragPairIter->aln1[i] == '-') // insert
						{
							ind_len = 1; while (FragPairIter->aln1[i + ind_len] == '-') ind_len++;
							frag2 = QueryChrVec[QueryChrIdx].seq.substr(qPos - 1, ind_len + 1);
							fprintf(outFile, "%s\t%d\t.\t%c\t%s\t100\tPASS\tmt=INSERT\n", RefChrName.c_str(), GenCoordinateInfo(rPos - 1).gPos, frag2[0], (char*)frag2.c_str());
							qPos += ind_len; i += ind_len - 1;
						}
						else if (FragPairIter->aln2[i] == '-') // delete
						{
							ind_len = 1; while (FragPairIter->aln2[i + ind_len] == '-') ind_len++;
							frag1.resize(ind_len + 2); frag1[ind_len + 1] = '\0';
							strncpy((char*)frag1.c_str(), RefSequence + rPos - 1, ind_len + 1);
							fprintf(outFile, "%s\t%d\t.\t%s\t%c\t100\tPASS\tmt=DELETE\n", RefChrName.c_str(), GenCoordinateInfo(rPos - 1).gPos, (char*)frag1.c_str(), frag1[0]);
							rPos += ind_len; i += ind_len - 1;
						}
						else if (nst_nt4_table[FragPairIter->aln1[i]] != nst_nt4_table[FragPairIter->aln2[i]])// substitute
						{
							fprintf(outFile, "%s\t%d\t.\t%c\t%c\t100\tPASS\tmt=SUBSTITUTE\n", RefChrName.c_str(), GenCoordinateInfo(rPos).gPos, FragPairIter->aln1[i], FragPairIter->aln2[i]);
							rPos++; qPos++;
						}
					}
				}
			}
		}
	}
	std::fclose(outFile);
}

void OutputDotplot()
{
	FILE *outFile;
	vector<int> vec;
	int64_t last_ref_end;
	string cmd, DataFileName;
	map<int, int> ChrColorMap;
	map<int, FILE*> ChrFileHandle;
	vector<AlnBlock_t>::iterator ABiter;
	vector<pair<int, int64_t> > ChrScoreVec;
	int i, j, last_query_end, FragNum, num, ChrIdx, thr;

	vec.resize(iChromsomeNum);
	outFile = fopen(gpFileName, "w"); DataFileName = (string)OutputPrefix + "." + QueryChrVec[QueryChrIdx].name;

	for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++) if (ABiter->score > 0) vec[ABiter->coor.ChromosomeIdx] += ABiter->score;
	for (i = 0; i < iChromsomeNum; i++) if (vec[i] >= 10000) ChrScoreVec.push_back(make_pair(i, vec[i]));
	sort(ChrScoreVec.begin(), ChrScoreVec.end(), CompByChrScore); if (ChrScoreVec.size() > 10) ChrScoreVec.resize(10); thr = ChrScoreVec.rbegin()->second;
	for (i = 0; i < (int)ChrScoreVec.size(); i++)
	{
		ChrColorMap[ChrScoreVec[i].first] = i + 1;
		ChrFileHandle[ChrScoreVec[i].first] = fopen((DataFileName + "vs" + (string)ChromosomeVec[ChrScoreVec[i].first].name).c_str(), "w");
		fprintf(ChrFileHandle[ChrScoreVec[i].first], "0 0\n0 0\n\n");
	}
	fprintf(outFile, "set terminal postscript color solid 'Courier' 15\nset output '%s-%s.ps'\n", OutputPrefix, QueryChrVec[QueryChrIdx].name.c_str());
	fprintf(outFile, "set grid\nset border 1\n");
	for (i = 0; i < (int)ChrScoreVec.size(); i++) fprintf(outFile, "set style line %d lw 4 pt 0 ps 0.5 lc '%s'\n", i + 1, LineColorArr[i].c_str());
	fprintf(outFile, "set xrange[1:*]\nset yrange[1:*]\nset xlabel 'Query (%s)'\nset ylabel 'Ref'\n", (char*)QueryChrVec[QueryChrIdx].name.c_str());
	fprintf(outFile, "plot "); for (i = 0; i < (int)ChrScoreVec.size(); i++) fprintf(outFile, "'%s' title '%s' with lp ls %d%s", (DataFileName + "vs" + (string)ChromosomeVec[ChrScoreVec[i].first].name).c_str(), ChromosomeVec[ChrScoreVec[i].first].name, ChrColorMap[ChrScoreVec[i].first], (i != (int)ChrScoreVec.size() - 1 ? ", " : "\n\n"));
	
	for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++)
	{
		if (ABiter->score > 0 && vec[ABiter->coor.ChromosomeIdx] >= thr)
		{
			FragNum = (int)ABiter->FragPairVec.size() - 1;
			last_query_end = ABiter->FragPairVec[FragNum].qPos + ABiter->FragPairVec[FragNum].qLen - 1;
			last_ref_end = ABiter->FragPairVec[FragNum].rPos + ABiter->FragPairVec[FragNum].rLen - 1;
			if (ABiter->coor.bDir) fprintf(ChrFileHandle[ABiter->coor.ChromosomeIdx], "%d %d\n%d %d\n\n", ABiter->FragPairVec[0].qPos + 1, GenCoordinateInfo(ABiter->FragPairVec[0].rPos).gPos, last_query_end + 1, GenCoordinateInfo(last_ref_end).gPos);
			else fprintf(ChrFileHandle[ABiter->coor.ChromosomeIdx], "%d %d\n%d %d\n\n", ABiter->FragPairVec[0].qPos + 1, GenCoordinateInfo(ABiter->FragPairVec[0].rPos).gPos, last_query_end + 1, GenCoordinateInfo(last_ref_end).gPos);
		}
	}
	for (i = 0; i < (int)ChrScoreVec.size(); i++) fclose(ChrFileHandle[ChrScoreVec[i].first]); fclose(outFile);
	cmd = (string)GnuPlotPath + " " + (string)gpFileName; system((char*)cmd.c_str());
	cmd = "rm " + DataFileName + "*"; system(cmd.c_str());
}

void CheckOverlaps(vector<FragPair_t>& FragPairVec)
{
	bool bOverlap = false;
	int64_t rPos;
	int i, qPos, len;
	map<int, bool> qMap;
	map<int64_t, bool> rMap;

	for (vector<FragPair_t>::iterator iter = FragPairVec.begin(); iter != FragPairVec.end(); iter++)
	{
		for (qPos = iter->qPos, i = 0; i < iter->qLen; i++, qPos++)
		{
			if (qMap.find(qPos) == qMap.end()) qMap.insert(make_pair(qPos, false));
			else
			{
				bOverlap = true;
				printf("q_error@%d, FragID=%d\n", qPos, iter - FragPairVec.begin());
				break;
			}
		}
		for (rPos = iter->rPos, i = 0; i < iter->rLen; i++, rPos++)
		{
			if (rMap.find(rPos) == rMap.end()) rMap.insert(make_pair(rPos, false));
			else
			{
				bOverlap = true;
				printf("r_error@%lld, FragID=%d\n", rPos, iter - FragPairVec.begin());
				break;
			}
		}
	}
	if (bOverlap) ShowFragPairVec(FragPairVec);
}

void GenomeComparison()
{
	int i, n;
	bool* CoverageArr;
	vector<AlnBlock_t>::iterator ABiter;
	int64_t iTotalQueryLength = 0, iCoverage = 0;
	pthread_t *ThreadArr = new pthread_t[iThreadNum];
	
	vector<int> vec(iThreadNum); for (i = 0; i < iThreadNum; i++) vec[i] = i;

	if (bDebugMode) iThreadNum = 1;

	if (OutputFormat == 0)
	{
		FILE *outFile;
		outFile = fopen(mafFileName, "w");
		fprintf(outFile, "##maf version=1\n");
		fclose(outFile);
	}
	fprintf(stderr, "Step2. Sequence analysis for all query chromosomes\n");
	for (QueryChrIdx = 0; QueryChrIdx != iQueryChrNum; QueryChrIdx++)
	{
		fprintf(stderr, "\tProcess query chromsomoe: %s...\n", QueryChrVec[QueryChrIdx].name.c_str());

		SeedVec.clear(); AlnBlockVec.clear();
		for (i = 0; i < iThreadNum; i++) pthread_create(&ThreadArr[i], NULL, IdentifyLocalMEM, &vec[i]);
		for (i = 0; i < iThreadNum; i++) pthread_join(ThreadArr[i], NULL);

		//for (vector<FragPair_t>::iterator iter = SeedVec.begin(); iter != SeedVec.end(); iter++) printf("q[%d-%d] r[%lld-%lld] len=%d PD=%lld\n", iter->qPos, iter->qPos + iter->qLen - 1, iter->rPos, iter->rPos + iter->rLen - 1, iter->qLen, iter->PosDiff);
		AlignmentBlockClustering();
		//for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++) ShowFragPairVec(ABiter->FragPairVec);
		////RemoveRedundantAlnBlocks();
		for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++) if (RemoveOverlaps(ABiter->FragPairVec)) RemoveNullFragPairs(ABiter->FragPairVec);
		for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++) IdentifyNormalPairs(ABiter->FragPairVec);
		//for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++) CheckOverlaps(ABiter->FragPairVec);
		fprintf(stderr, "\t\tGenreate sequence alignment...\n");
		for (i = 0; i < iThreadNum; i++) pthread_create(&ThreadArr[i], NULL, GenerateFragAlignment, &vec[i]);
		for (i = 0; i < iThreadNum; i++) pthread_join(ThreadArr[i], NULL);
		CoverageArr = new bool[QueryChrVec[QueryChrIdx].seq.length()]();
		for (ABiter = AlnBlockVec.begin(); ABiter != AlnBlockVec.end(); ABiter++)
		{
			n = (int)ABiter->FragPairVec.size() - 1;
			memset((CoverageArr + ABiter->FragPairVec[0].qPos), true, (int)(ABiter->FragPairVec[n].qPos + ABiter->FragPairVec[n].qLen - ABiter->FragPairVec[0].qPos));
			ABiter->coor = GenCoordinateInfo(ABiter->FragPairVec[0].rPos);
		}
		if (OutputFormat == 0) fprintf(stderr, "\tOutput the MAF for query chromosome %s in the file: %s\n", QueryChrVec[QueryChrIdx].name.c_str(), mafFileName),OutputMAF();
		if (OutputFormat == 1) fprintf(stderr, "\tOutput the alignment for query chromosome %s in the file: %s\n", QueryChrVec[QueryChrIdx].name.c_str(), alnFileName),OutputAlignment();
		fprintf(stderr, "\tOutput the variants for query chromosome %s in the file: %s\n", QueryChrVec[QueryChrIdx].name.c_str(), vcfFileName); OutputVariantCallingFile();
		if (bShowPlot && GnuPlotPath != NULL) fprintf(stderr, "\tGenerate the dotplot for query chromosome %s in the file: %s-%s.ps\n", QueryChrVec[QueryChrIdx].name.c_str(), OutputPrefix, QueryChrVec[QueryChrIdx].name.c_str()), OutputDotplot();

		iTotalQueryLength += (n = (int)QueryChrVec[QueryChrIdx].seq.length());
		for (i = 0; i < n; i++) if (CoverageArr[i]) iCoverage++;
		delete[] CoverageArr;
	}
	if (LocalAlignmentNum > 0 && iTotalQueryLength > 0) fprintf(stderr, "\nAlignment # = %lld, Total alignment length = %lld (avgLen=%lld), coverage = %.2f%%\n", (long long)LocalAlignmentNum, (long long)TotalAlignmentLength, (long long)(TotalAlignmentLength/ LocalAlignmentNum), 100*(1.0*iCoverage / iTotalQueryLength));

	delete[] ThreadArr;
}
