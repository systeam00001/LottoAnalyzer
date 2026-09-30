#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QStringConverter>

#include <array>
#include <vector>
#include <algorithm>
#include <random>
#include <set>
#include <unordered_map>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct LottoRound
{
    int round = 0;
    std::array<int, 6> numbers{};
};

struct MatchResult
{
    int round = 0;
    int matchCount = 0;
    std::vector<int> matchedNumbers;
};

struct HistoricalStats
{
    std::array<int, 46> numberFrequency{};

    std::array<
        std::array<int, 46>,
        46> pairFrequency{};

    std::unordered_map<int, int> tripleFrequency;
};

struct FeatureVector
{
    // 6개 번호 각각의 전체 출현 빈도 평균
    double numberFrequencyAverage = 0.0;

    // 후보 안의 15개 pair가 과거에 같이 나온 빈도 평균
    double pairFrequencyAverage = 0.0;

    // 후보 안의 20개 triple이 과거에 같이 나온 빈도 평균
    double tripleFrequencyAverage = 0.0;

    // 과거 당첨번호와 정확히 N개 일치한 횟수
    int exact4Count = 0;
    int exact5Count = 0;
    int exact6Count = 0;
};

struct ReferenceStats
{
    double numberMean = 0.0;
    double numberStdDev = 0.0;

    double pairMean = 0.0;
    double pairStdDev = 0.0;

    double tripleMean = 0.0;
    double tripleStdDev = 0.0;

    double exact4Mean = 0.0;
    double exact4StdDev = 0.0;

    double exact5PlusMean = 0.0;
    double exact5PlusStdDev = 0.0;
};

struct CandidateScore
{
    std::array<int, 6> numbers{};
    FeatureVector features{};

    // 낮을수록 과거 당첨 조합들의 평균적 특성에 가까움
    double score = 0.0;
};

static int encodeTriple(
    int a,
    int b,
    int c)
{
    return (a * 10000) +
           (b * 100) +
           c;
}

static bool loadLottoCsv(
    const QString& fileName,
    std::vector<LottoRound>& rounds)
{
    QFile file(fileName);

    if (!file.open(
            QIODevice::ReadOnly |
            QIODevice::Text))
    {
        QTextStream err(stderr);

        err << "CSV file open failed: "
            << fileName
            << Qt::endl;

        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    bool firstLine = true;

    while (!in.atEnd())
    {
        const QString line =
            in.readLine().trimmed();

        if (line.isEmpty())
        {
            continue;
        }

        if (firstLine)
        {
            firstLine = false;

            if (line.startsWith(
                    QStringLiteral("회차")))
            {
                continue;
            }
        }

        const QStringList fields =
            line.split(',');

        if (fields.size() < 7)
        {
            continue;
        }

        LottoRound item;

        bool ok = false;

        item.round =
            fields[0]
                .trimmed()
                .toInt(&ok);

        if (!ok)
        {
            continue;
        }

        bool valid = true;

        for (int i = 0; i < 6; ++i)
        {
            item.numbers[i] =
                fields[i + 1]
                    .trimmed()
                    .toInt(&ok);

            if (!ok ||
                item.numbers[i] < 1 ||
                item.numbers[i] > 45)
            {
                valid = false;
                break;
            }
        }

        if (!valid)
        {
            continue;
        }

        std::sort(
            item.numbers.begin(),
            item.numbers.end());

        // 동일 번호가 한 회차에 중복되어 있으면 오류
        if (std::adjacent_find(
                item.numbers.begin(),
                item.numbers.end()) !=
            item.numbers.end())
        {
            continue;
        }

        rounds.push_back(item);
    }

    std::sort(
        rounds.begin(),
        rounds.end(),
        [](const LottoRound& a,
           const LottoRound& b)
        {
            return a.round < b.round;
        });

    return !rounds.empty();
}

static const LottoRound* findRound(
    const std::vector<LottoRound>& rounds,
    int targetRound)
{
    for (const auto& item : rounds)
    {
        if (item.round == targetRound)
        {
            return &item;
        }
    }

    return nullptr;
}

static int countMatches(
    const std::array<int, 6>& a,
    const std::array<int, 6>& b)
{
    int count = 0;

    for (int number : a)
    {
        if (std::find(
                b.begin(),
                b.end(),
                number) != b.end())
        {
            ++count;
        }
    }

    return count;
}

static MatchResult compareRound(
    const LottoRound& base,
    const LottoRound& target)
{
    MatchResult result;

    result.round = target.round;

    for (int number : base.numbers)
    {
        if (std::find(
                target.numbers.begin(),
                target.numbers.end(),
                number) !=
            target.numbers.end())
        {
            ++result.matchCount;

            result.matchedNumbers.push_back(
                number);
        }
    }

    return result;
}

static void printNumbers(
    QTextStream& out,
    const std::array<int, 6>& numbers)
{
    for (std::size_t i = 0;
         i < numbers.size();
         ++i)
    {
        if (i > 0)
        {
            out << ' ';
        }

        out << numbers[i];
    }
}

static void printMatchedNumbers(
    QTextStream& out,
    const std::vector<int>& numbers)
{
    for (std::size_t i = 0;
         i < numbers.size();
         ++i)
    {
        if (i > 0)
        {
            out << ' ';
        }

        out << numbers[i];
    }
}

static void analyzeRound(
    QTextStream& out,
    const std::vector<LottoRound>& rounds,
    const LottoRound& baseRound,
    bool onlyLaterRounds)
{
    std::vector<MatchResult> results;

    for (const auto& target : rounds)
    {
        if (target.round ==
            baseRound.round)
        {
            continue;
        }

        /*
         * 0 = 전체 분석일 때
         *
         * 동일한 회차 쌍은 한 번만 비교
         */
        if (onlyLaterRounds &&
            target.round < baseRound.round)
        {
            continue;
        }

        MatchResult result =
            compareRound(
                baseRound,
                target);

        if (result.matchCount >= 4)
        {
            results.push_back(
                std::move(result));
        }
    }

    /*
     * 전체 분석에서 결과가 없으면
     * 기준 회차 자체를 출력하지 않는다.
     */
    if (results.empty() &&
        onlyLaterRounds)
    {
        return;
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const MatchResult& a,
           const MatchResult& b)
        {
            if (a.matchCount !=
                b.matchCount)
            {
                return a.matchCount >
                       b.matchCount;
            }

            return a.round < b.round;
        });

    out << Qt::endl;

    out << "========================================"
        << Qt::endl;

    out << "기준 회차 : "
        << baseRound.round
        << "회"
        << Qt::endl;

    out << "기준 번호 : ";

    printNumbers(
        out,
        baseRound.numbers);

    out << Qt::endl;

    out << "=== 4개 이상 일치 ==="
        << Qt::endl;

    if (results.empty())
    {
        out << "없음"
            << Qt::endl;
    }
    else
    {
        for (const auto& result : results)
        {
            out << result.round
                << "회 : ";

            printMatchedNumbers(
                out,
                result.matchedNumbers);

            out << " ("
                << result.matchCount
                << "개)"
                << Qt::endl;
        }
    }

    out << "========================================"
        << Qt::endl;
}

static HistoricalStats buildHistoricalStats(
    const std::vector<LottoRound>& rounds)
{
    HistoricalStats stats;

    for (const auto& round : rounds)
    {
        /*
         * 개별 번호 빈도
         */
        for (int number : round.numbers)
        {
            ++stats.numberFrequency[number];
        }

        /*
         * 2개 조합 빈도
         *
         * 6C2 = 15개
         */
        for (int i = 0; i < 6; ++i)
        {
            for (int j = i + 1;
                 j < 6;
                 ++j)
            {
                const int a =
                    round.numbers[i];

                const int b =
                    round.numbers[j];

                ++stats.pairFrequency[a][b];
            }
        }

        /*
         * 3개 조합 빈도
         *
         * 6C3 = 20개
         */
        for (int i = 0; i < 6; ++i)
        {
            for (int j = i + 1;
                 j < 6;
                 ++j)
            {
                for (int k = j + 1;
                     k < 6;
                     ++k)
                {
                    const int key =
                        encodeTriple(
                            round.numbers[i],
                            round.numbers[j],
                            round.numbers[k]);

                    ++stats.tripleFrequency[key];
                }
            }
        }
    }

    return stats;
}

static int getTripleFrequency(
    const HistoricalStats& stats,
    int a,
    int b,
    int c)
{
    const int key =
        encodeTriple(
            a,
            b,
            c);

    const auto it =
        stats.tripleFrequency.find(key);

    if (it ==
        stats.tripleFrequency.end())
    {
        return 0;
    }

    return it->second;
}

static FeatureVector calculateFeatures(
    const std::array<int, 6>& numbers,
    const std::vector<LottoRound>& rounds,
    const HistoricalStats& stats,
    int skipIndex = -1)
{
    FeatureVector features;

    /*
     * ------------------------------------------------
     * 1. 개별 번호 출현 빈도
     * ------------------------------------------------
     */
    int numberFrequencySum = 0;

    for (int number : numbers)
    {
        numberFrequencySum +=
            stats.numberFrequency[number];
    }

    features.numberFrequencyAverage =
        static_cast<double>(
            numberFrequencySum) /
        6.0;

    /*
     * ------------------------------------------------
     * 2. Pair 빈도
     * ------------------------------------------------
     *
     * 6개 번호 안에는 총 15개의 pair가 있음.
     */
    int pairFrequencySum = 0;
    int pairCount = 0;

    for (int i = 0; i < 6; ++i)
    {
        for (int j = i + 1;
             j < 6;
             ++j)
        {
            pairFrequencySum +=
                stats.pairFrequency
                    [numbers[i]]
                    [numbers[j]];

            ++pairCount;
        }
    }

    features.pairFrequencyAverage =
        static_cast<double>(
            pairFrequencySum) /
        static_cast<double>(
            pairCount);

    /*
     * ------------------------------------------------
     * 3. Triple 빈도
     * ------------------------------------------------
     *
     * 6개 번호 안에는 총 20개의 triple이 있음.
     */
    int tripleFrequencySum = 0;
    int tripleCount = 0;

    for (int i = 0; i < 6; ++i)
    {
        for (int j = i + 1;
             j < 6;
             ++j)
        {
            for (int k = j + 1;
                 k < 6;
                 ++k)
            {
                tripleFrequencySum +=
                    getTripleFrequency(
                        stats,
                        numbers[i],
                        numbers[j],
                        numbers[k]);

                ++tripleCount;
            }
        }
    }

    features.tripleFrequencyAverage =
        static_cast<double>(
            tripleFrequencySum) /
        static_cast<double>(
            tripleCount);

    /*
     * ------------------------------------------------
     * 4. 과거 당첨번호와 4/5/6개 일치 횟수
     * ------------------------------------------------
     */
    for (int i = 0;
         i < static_cast<int>(rounds.size());
         ++i)
    {
        /*
         * 과거 실제 회차 자체의 특징을 계산할 때는
         * 자기 자신과의 6개 일치를 제외한다.
         */
        if (i == skipIndex)
        {
            continue;
        }

        const int matchCount =
            countMatches(
                numbers,
                rounds[i].numbers);

        if (matchCount == 4)
        {
            ++features.exact4Count;
        }
        else if (matchCount == 5)
        {
            ++features.exact5Count;
        }
        else if (matchCount == 6)
        {
            ++features.exact6Count;
        }
    }

    return features;
}

static double calculateMean(
    const std::vector<double>& values)
{
    if (values.empty())
    {
        return 0.0;
    }

    double sum = 0.0;

    for (double value : values)
    {
        sum += value;
    }

    return sum /
           static_cast<double>(
               values.size());
}

static double calculateStdDev(
    const std::vector<double>& values,
    double mean)
{
    if (values.empty())
    {
        return 0.0;
    }

    double sum = 0.0;

    for (double value : values)
    {
        const double diff =
            value - mean;

        sum += diff * diff;
    }

    return std::sqrt(
        sum /
        static_cast<double>(
            values.size()));
}

static ReferenceStats buildReferenceStats(
    const std::vector<LottoRound>& rounds,
    const HistoricalStats& historicalStats)
{
    std::vector<double> numberValues;
    std::vector<double> pairValues;
    std::vector<double> tripleValues;
    std::vector<double> exact4Values;
    std::vector<double> exact5PlusValues;

    numberValues.reserve(
        rounds.size());

    pairValues.reserve(
        rounds.size());

    tripleValues.reserve(
        rounds.size());

    exact4Values.reserve(
        rounds.size());

    exact5PlusValues.reserve(
        rounds.size());

    /*
     * 실제 1회 ~ 마지막 회차 각각의 특성을 계산한다.
     *
     * 즉 후보 번호가 실제 과거 당첨번호들과
     * 얼마나 비슷한 성격인지 비교하기 위한
     * 기준 분포를 만든다.
     */
    for (int i = 0;
         i < static_cast<int>(rounds.size());
         ++i)
    {
        const FeatureVector features =
            calculateFeatures(
                rounds[i].numbers,
                rounds,
                historicalStats,
                i);

        numberValues.push_back(
            features.numberFrequencyAverage);

        pairValues.push_back(
            features.pairFrequencyAverage);

        tripleValues.push_back(
            features.tripleFrequencyAverage);

        exact4Values.push_back(
            static_cast<double>(
                features.exact4Count));

        exact5PlusValues.push_back(
            static_cast<double>(
                features.exact5Count +
                features.exact6Count));
    }

    ReferenceStats reference;

    reference.numberMean =
        calculateMean(
            numberValues);

    reference.numberStdDev =
        calculateStdDev(
            numberValues,
            reference.numberMean);

    reference.pairMean =
        calculateMean(
            pairValues);

    reference.pairStdDev =
        calculateStdDev(
            pairValues,
            reference.pairMean);

    reference.tripleMean =
        calculateMean(
            tripleValues);

    reference.tripleStdDev =
        calculateStdDev(
            tripleValues,
            reference.tripleMean);

    reference.exact4Mean =
        calculateMean(
            exact4Values);

    reference.exact4StdDev =
        calculateStdDev(
            exact4Values,
            reference.exact4Mean);

    reference.exact5PlusMean =
        calculateMean(
            exact5PlusValues);

    reference.exact5PlusStdDev =
        calculateStdDev(
            exact5PlusValues,
            reference.exact5PlusMean);

    return reference;
}

static double normalizedDistance(
    double value,
    double mean,
    double stdDev)
{
    constexpr double EPSILON =
        0.0000001;

    if (stdDev < EPSILON)
    {
        if (std::abs(
                value - mean) <
            EPSILON)
        {
            return 0.0;
        }

        return 10.0 +
               std::abs(
                   value - mean);
    }

    return std::abs(
               value - mean) /
           stdDev;
}

static CandidateScore evaluateCandidate(
    const std::array<int, 6>& numbers,
    const std::vector<LottoRound>& rounds,
    const HistoricalStats& historicalStats,
    const ReferenceStats& referenceStats)
{
    CandidateScore result;

    result.numbers =
        numbers;

    result.features =
        calculateFeatures(
            numbers,
            rounds,
            historicalStats);

    /*
     * ===================================================
     * 후보 평가 점수
     * ===================================================
     *
     * 낮을수록 실제 과거 당첨 조합들의
     * 평균적인 성격에 가깝다.
     *
     * 번호 빈도      : 30%
     * Pair 빈도      : 25%
     * Triple 빈도    : 20%
     * 4개 중복 횟수  : 15%
     * 5개 이상 중복 : 10%
     *
     * 중요한 점:
     *
     * 4개가 과거 번호와 겹친다고 무조건 제거하지 않는다.
     *
     * 실제 과거 당첨번호끼리도 4개/5개가 겹치는 경우가
     * 존재하므로 "과거 당첨번호들이 보였던 평균적인
     * 중복 정도"에 가까운지를 평가한다.
     */

    const double numberDistance =
        normalizedDistance(
            result.features
                .numberFrequencyAverage,
            referenceStats.numberMean,
            referenceStats.numberStdDev);

    const double pairDistance =
        normalizedDistance(
            result.features
                .pairFrequencyAverage,
            referenceStats.pairMean,
            referenceStats.pairStdDev);

    const double tripleDistance =
        normalizedDistance(
            result.features
                .tripleFrequencyAverage,
            referenceStats.tripleMean,
            referenceStats.tripleStdDev);

    const double exact4Distance =
        normalizedDistance(
            static_cast<double>(
                result.features.exact4Count),
            referenceStats.exact4Mean,
            referenceStats.exact4StdDev);

    const double exact5PlusDistance =
        normalizedDistance(
            static_cast<double>(
                result.features.exact5Count +
                result.features.exact6Count),
            referenceStats.exact5PlusMean,
            referenceStats.exact5PlusStdDev);

    result.score =
        (numberDistance * 0.30) +
        (pairDistance * 0.25) +
        (tripleDistance * 0.20) +
        (exact4Distance * 0.15) +
        (exact5PlusDistance * 0.10);

    /*
     * 과거와 완전히 같은 6개 번호인 경우에는
     * 약간의 추가 penalty를 준다.
     *
     * 무조건 제거하지는 않는다.
     */
    if (result.features.exact6Count > 0)
    {
        result.score +=
            static_cast<double>(
                result.features.exact6Count) *
            2.0;
    }

    return result;
}

static std::array<int, 6>
generateRandomNumbers(
    std::mt19937& generator)
{
    std::array<int, 45> pool{};

    for (int i = 0; i < 45; ++i)
    {
        pool[i] = i + 1;
    }

    std::shuffle(
        pool.begin(),
        pool.end(),
        generator);

    std::array<int, 6> result{};

    std::copy_n(
        pool.begin(),
        6,
        result.begin());

    std::sort(
        result.begin(),
        result.end());

    return result;
}

static void generateCandidates(
    QTextStream& in,
    QTextStream& out,
    const std::vector<LottoRound>& rounds)
{
    out << "생성할 후보 개수 입력: ";
    out.flush();

    bool ok = false;

    const int outputCandidateCount =
        in.readLine()
            .trimmed()
            .toInt(&ok);

    if (!ok ||
        outputCandidateCount <= 0)
    {
        out << "잘못된 개수입니다."
            << Qt::endl;

        return;
    }

    /*
     * 랜덤 조합을 이만큼 만들어
     * 역사적 특성과 비교한다.
     */
    constexpr int SAMPLE_COUNT =
        100000;

    out << Qt::endl;
    out << "과거 데이터 통계 계산 중..."
        << Qt::endl;

    const HistoricalStats historicalStats =
        buildHistoricalStats(
            rounds);

    const ReferenceStats referenceStats =
        buildReferenceStats(
            rounds,
            historicalStats);

    out << "후보 "
        << SAMPLE_COUNT
        << "개 평가..."
        << Qt::endl;

    std::random_device rd;

    std::mt19937 generator(
        rd());

    std::set<std::array<int, 6>>
        generatedSet;

    std::vector<CandidateScore>
        candidates;

    candidates.reserve(
        SAMPLE_COUNT);

    while (static_cast<int>(
               candidates.size()) <
           SAMPLE_COUNT)
    {
        const auto numbers =
            generateRandomNumbers(
                generator);

        /*
         * 이번 실행에서 이미 평가한 번호라면 생략
         */
        if (!generatedSet
                 .insert(numbers)
                 .second)
        {
            continue;
        }

        CandidateScore score =
            evaluateCandidate(
                numbers,
                rounds,
                historicalStats,
                referenceStats);

        candidates.push_back(
            std::move(score));
    }

    /*
     * 점수가 낮은 후보 우선
     */
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const CandidateScore& a,
           const CandidateScore& b)
        {
            if (a.score != b.score)
            {
                return a.score <
                       b.score;
            }

            return a.numbers <
                   b.numbers;
        });

    const int outputCount =
        std::min(
            outputCandidateCount,
            static_cast<int>(
                candidates.size()));

    out.setRealNumberNotation(
        QTextStream::FixedNotation);

    out.setRealNumberPrecision(3);

    out << Qt::endl;

    out << "========================================"
        << Qt::endl;

    out << "과거 당첨번호 기준값"
        << Qt::endl;

    out << "----------------------------------------"
        << Qt::endl;

    out << "번호 평균 빈도 : "
        << referenceStats.numberMean
        << Qt::endl;

    out << "Pair 평균 빈도 : "
        << referenceStats.pairMean
        << Qt::endl;

    out << "Triple 평균 빈도 : "
        << referenceStats.tripleMean
        << Qt::endl;

    out << "4개 중복 평균 : "
        << referenceStats.exact4Mean
        << "회"
        << Qt::endl;

    out << "5개 이상 중복 평균 : "
        << referenceStats.exact5PlusMean
        << "회"
        << Qt::endl;

    out << "========================================"
        << Qt::endl;

    out << Qt::endl;

    out << "후보 번호"
        << Qt::endl;

    out << "※ 점수가 낮을수록 과거 당첨 조합의"
           " 평균적인 특성에 가까움"
        << Qt::endl;

    out << "========================================"
        << Qt::endl;

    for (int i = 0;
         i < outputCount;
         ++i)
    {
        const CandidateScore& candidate =
            candidates[i];

        out << Qt::endl;

        out << (i + 1)
            << " : ";

        printNumbers(
            out,
            candidate.numbers);

        out << Qt::endl;

        out << "    점수             : "
            << candidate.score
            << Qt::endl;

        out << "    번호 평균 빈도   : "
            << candidate.features
                   .numberFrequencyAverage
            << Qt::endl;

        out << "    Pair 평균 빈도   : "
            << candidate.features
                   .pairFrequencyAverage
            << Qt::endl;

        out << "    Triple 평균 빈도 : "
            << candidate.features
                   .tripleFrequencyAverage
            << Qt::endl;

        out << "    과거 4개 일치    : "
            << candidate.features
                   .exact4Count
            << "회"
            << Qt::endl;

        out << "    과거 5개 일치    : "
            << candidate.features
                   .exact5Count
            << "회"
            << Qt::endl;

        out << "    과거 6개 일치    : "
            << candidate.features
                   .exact6Count
            << "회"
            << Qt::endl;
    }

    out << Qt::endl;

    out << "========================================"
        << Qt::endl;
}

int main(
    int argc,
    char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(
        CP_UTF8);

    SetConsoleCP(
        CP_UTF8);
#endif

    QCoreApplication app(
        argc,
        argv);

    QTextStream in(stdin);
    QTextStream out(stdout);

    in.setEncoding(
        QStringConverter::Utf8);

    out.setEncoding(
        QStringConverter::Utf8);

    std::vector<LottoRound> rounds;

    const QString csvFileName =
        QStringLiteral("lotto.csv");

    if (!loadLottoCsv(
            csvFileName,
            rounds))
    {
        out << "CSV 데이터를 읽을 수 없습니다."
            << Qt::endl;

        return 1;
    }

    out << "Loaded "
        << rounds.size()
        << " rounds."
        << Qt::endl;

    out << Qt::endl;

    out << "기준 회차 입력"
        << Qt::endl;

    out << "  1 이상 : 해당 회차 분석"
        << Qt::endl;

    out << "  0      : 전체 회차 분석"
        << Qt::endl;

    out << " -1      : 후보 번호 생성"
        << Qt::endl;

    out << "> ";
    out.flush();

    bool ok = false;

    const int command =
        in.readLine()
            .trimmed()
            .toInt(&ok);

    if (!ok)
    {
        out << "잘못된 입력입니다."
            << Qt::endl;

        return 1;
    }

    /*
     * 후보 번호 생성
     */
    if (command == -1)
    {
        generateCandidates(
            in,
            out,
            rounds);

        return 0;
    }

    /*
     * 전체 회차 분석
     */
    if (command == 0)
    {
        for (const auto& baseRound :
             rounds)
        {
            analyzeRound(
                out,
                rounds,
                baseRound,
                true);
        }

        return 0;
    }

    if (command < 0)
    {
        out << "잘못된 입력입니다."
            << Qt::endl;

        return 1;
    }

    /*
     * 특정 회차 분석
     */
    const LottoRound* baseRound =
        findRound(
            rounds,
            command);

    if (baseRound == nullptr)
    {
        out << command
            << "회 데이터를 찾을 수 없습니다."
            << Qt::endl;

        return 1;
    }

    analyzeRound(
        out,
        rounds,
        *baseRound,
        false);

    return 0;
}